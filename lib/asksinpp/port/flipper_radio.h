//- ---------------------------------------------------------------------------
//  AskSin++ Radio-Layer auf dem internen CC1101 des Flipper Zero
//
//  Dieser Typ ERSETZT upstream/Radio.h komplett -- er erbt nicht davon.
//  Begruendung:
//
//    upstream/Radio<> bringt einen kompletten AVR-Apparat mit: eigene
//    SPI-Implementierung, attachInterrupt() auf GDO0, ISR-getriebene
//    Zustandsbits. Auf dem Flipper ist all das ueberfluessig, weil die
//    Firmware den CC1101 bereits im Packet-Mode exportiert
//    (subghz_devices_write_packet/read_packet/rx_pipe_not_empty/...).
//    AskSin nimmt den Radio als reinen Template-Parameter
//    (AskSin<Led,Battery,Radio,Buzzer>) -- eine eigene Klasse mit derselben
//    oeffentlichen Schnittstelle genuegt also vollstaendig.
//
//  Die Schnittstelle unten deckt ab, was upstream/NoRadio.h vorgibt und was
//  Device.h/AskSinPP.h tatsaechlich aufrufen (read, write, rssi,
//  setSendTimeout, init, enable, setIdle, wakeup, waitTimeout, tuneFreq).
//
//  WARUM furi_hal_subghz_* UND NICHT subghz_devices_*:
//  Der erste Entwurf ging ueber die Device-Registry (subghz_devices_begin
//  usw.). Auf der Zielhardware schlug subghz_devices_begin() fehl, waehrend
//  das Log gleichzeitig "Library for subghz_radio_device, API v. 2" meldete
//  -- der ufbt-SDK-Header definiert aber
//  SUBGHZ_RADIO_DEVICE_PLUGIN_API_VERSION 1. Die Registry-Semantik weicht
//  auf dieser Firmware also ab.
//
//  Der Packet-Mode ist vollstaendig auch ohne Registry erreichbar: alle hier
//  benutzten furi_hal_subghz_*-Funktionen stehen in api_symbols.csv mit '+'.
//  Einzige Ausnahme ist furi_hal_subghz_init() ('-'), die wir nicht
//  brauchen -- die Firmware ruft sie beim Boot selbst auf, wir setzen mit
//  reset() darauf auf.
//
//  Preis dieser Entscheidung: kein externes CC1101-Modul. Das ist laut
//  CLAUDE.md §0 ohnehin ausserhalb des Scope.
//
//  THREADING: Alle Methoden gehoeren dem Radio-Thread. Aus GUI- oder
//  App-Thread darf hier nichts aufgerufen werden (CLAUDE.md §7.1).
//- ---------------------------------------------------------------------------
#pragma once

#include "flipper_config.h"

#include <Message.h>

#include <furi.h>
#include <furi_hal_subghz.h>

namespace as {

/** Ringpuffer fuer Diagnoseausgaben aus dem Funk-Zeitfenster.
 *
 *  CLAUDE.md §8.1 verbietet Logging zwischen TX und ACK-Empfang: ein
 *  FURI_LOG_I geht ueber die UART und kostet je nach Zeilenlaenge mehrere
 *  Millisekunden -- mitten im ~200-ms-ACK-Fenster ist das nicht harmlos,
 *  und in waitResponse() (Pollabstand 10 ms) verschiebt es den Lesezeitpunkt
 *  so weit, dass die Quittung verpasst werden kann.
 *
 *  Deshalb schreiben write()/read() nur noch in diesen Puffer. Ausgegeben
 *  wird er von hm_service_poll() ganz am Anfang der Schleife, also an
 *  demselben garantiert funkfreien Punkt, an dem auch das EEPROM-Flush
 *  laeuft (CLAUDE.md §6.2).
 *
 *  Schreiben ist allokationsfrei und darf nicht blockieren (§3.2). Laeuft
 *  der Puffer ueber, gehen die AELTESTEN Eintraege verloren und drain()
 *  meldet die Zahl der verworfenen -- lieber eine Luecke als ein
 *  verfaelschtes Timing.
 *
 *  THREADING: nur aus dem Radio-Thread benutzen (CLAUDE.md §7.1). */
class RadioTrace {
public:
    /** Rohtelegramm mitschneiden. Es wird der UNVERSCHLUESSELTE Inhalt
     *  erwartet (vor encode() bzw. nach decode()), sonst steht im Log nur
     *  die BidCos-Verschleierung.
     *  @param dir 'T' = gesendet, 'R' = empfangen
     *  @param rssi_dbm nur bei 'R' sinnvoll, sonst 0
     *  @param lqi Link Quality des Empfangs, sonst 0 */
    static void frame(char dir, const uint8_t* buf, uint8_t len, int16_t rssi_dbm, uint8_t lqi);

    /** Kurznotiz mit bis zu zwei Zahlenwerten.
     *  @param what Stringliteral -- wird NICHT kopiert, nur der Zeiger
     *              gemerkt. Niemals einen Stackpuffer uebergeben. */
    static void note(const char* what, uint32_t a = 0, uint32_t b = 0);

    /** Puffer ausgeben und leeren. NUR an funkfreier Stelle aufrufen. */
    static void drain();

private:
    struct Entry {
        uint32_t tick;
        const char* what; // NULL -> Telegramm, sonst Notiz
        uint32_t a;
        uint32_t b;
        int16_t rssi;
        uint8_t lqi;
        uint8_t len;
        char dir;
        uint8_t data[24]; // erste 24 Bytes reichen fuer Kopf + Anfang Nutzlast
    };

    // Als enum, nicht als 'static const uint8_t': so braucht die Konstante
    // keine Definition in der .cpp und kann nirgends odr-used werden.
    enum { CAPACITY = 16 };

    static Entry s_ring[CAPACITY];
    static uint8_t s_head; // naechster Schreibplatz
    static uint8_t s_count; // belegte Plaetze
    static uint16_t s_lost; // seit dem letzten drain() verworfene Eintraege

    static Entry& acquire();
};

class FlipperRadio {
public:
    /** Ausgang des letzten Sendevorgangs.
     *
     *  Existiert, weil Device::send() sein Ergebnis nur als bool an
     *  RemoteChannel::state() zurueckgibt -- und das ist void. Fuer die
     *  Anzeige (CLAUDE.md §7.3) waere die Quittung damit verloren.
     *  Wird in write() gesetzt und in read() zu TxAck/TxNack verfeinert. */
    enum TxResult : uint8_t {
        TxNone = 0, ///< seit dem Start nichts gesendet
        TxPending, ///< gesendet, Antwort steht noch aus
        TxSent, ///< gesendet, es wird keine Antwort erwartet (Broadcast)
        TxAck, ///< ACK der Gegenstelle empfangen
        TxNack, ///< NACK der Gegenstelle empfangen
        TxBlocked, ///< furi_hal_subghz_tx() von der Region verweigert
        TxFailed, ///< Aussendung selbst gescheitert
        /** Die Gegenstelle hat eine AES-Challenge geschickt.
         *
         *  ANGEHAENGT und nicht einsortiert, damit die Zahlenwerte der
         *  bestehenden Zustaende stabil bleiben -- sie stehen so im Log.
         *
         *  Das ist KEIN NACK und kein ACK. Die Unterscheidung ist nicht
         *  offensichtlich: eine Challenge ist AS_MESSAGE_RESPONSE (0x02)
         *  mit comm == AS_RESPONSE_AES_CHALLANGE (0x04), und
         *  Message::isAck() prueft nur '(comm & AS_RESPONSE_NACK) ==
         *  AS_RESPONSE_ACK', also '(0x04 & 0x80) == 0x00' -- das ist
         *  TRUE. Ohne die Sonderbehandlung in read() meldet die App also
         *  faelschlich einen erfolgreichen Sendevorgang, waehrend die
         *  Zentrale noch auf die Signatur wartet. */
        TxAesChallenge,
    };

    FlipperRadio();

    // -- Lebenszyklus -------------------------------------------------------

    /** CC1101 beschaffen, zuruecksetzen, AskSin-Preset laden, auf RX.
     *  @return true wenn das Geraet da war und die Frequenz zulaessig ist. */
    bool init();

    /** Gegenstueck zu init(). Muss vor App-Ende laufen, sonst bleibt der
     *  CC1101 belegt. */
    void deinit();

    // upstream ruft enable()/disable() rund um das Interrupt-Handling. Wir
    // pollen (CLAUDE.md §5.4), also sind beide bewusst leer.
    void enable() {
    }
    void disable() {
    }

    // -- Datenverkehr -------------------------------------------------------

    /** Nicht-blockierend ein Telegramm abholen.
     *  @return Laenge in msg, 0 wenn nichts anliegt. */
    uint8_t read(Message& msg);

    /** Telegramm senden. Blockiert bis zum Ende der Aussendung (bounded).
     *  @param burst wird derzeit nicht unterstuetzt, siehe .cpp
     *  @return false wenn die Region TX sperrt oder die Sendung scheitert. */
    bool write(const Message& msg, uint8_t burst);

    /** RSSI des letzten EMPFANGS in der Skala, die AskSin++ in Telegramme
     *  schreibt: der BETRAG des Pegels in dBm, also 60 fuer -60 dBm.
     *  0 = noch nichts empfangen.
     *
     *  Das ist NICHT der CC1101-Rohwert des RSSI-Registers. Upstream
     *  rechnet ihn in Radio-CC1101.h:469 um:
     *      rss = -1 * ((raw - (raw >= 128 ? 256 : 0)) / 2 - 74)
     *  Fuer raw = 28 ergibt das 60, nicht 28. Der Wert geht ueber
     *  Device::sendAck() und sendInfoActuatorStatus() an die Zentrale --
     *  mit dem Rohwert meldeten wir ihr durchgaengig einen um rund
     *  40 dB zu guten Pegel. */
    uint8_t rssi() const {
        return m_rssi;
    }

    /** true, sobald GDO0 mindestens einmal als high gesehen wurde.
     *
     *  Der schnellste Test, ob Regel 5.2.1 eingehalten ist: bleibt das
     *  dauerhaft false, obwohl gesendet und empfangen wird, haengt der Pin
     *  im Analogmodus fest (Schmitt-Trigger aus, furi_hal_gpio_read()
     *  liefert konstant 0) und sowohl das Warten auf das Sendeende als auch
     *  das Warten auf ein vollstaendiges Empfangspaket laufen blind. */
    bool gdo0Seen() const {
        return m_gdo0_seen;
    }

    /** Telegramme, die der CC1101 mit CRC-Fehler abgeliefert hat. */
    uint16_t rxCrcErrors() const {
        return m_rx_crc_err;
    }

    /** Wie oft read() auf ein noch einlaufendes Telegramm gewartet hat.
     *
     *  Steigt dieser Zaehler, arbeitet die Empfangsseite korrekt: genau
     *  dieses Warten hat vorher gefehlt (siehe .cpp, waitRxComplete). */
    uint16_t rxBusyWaits() const {
        return m_rx_busy;
    }

    /** Zahl der empfangenen AES-Challenges.
     *
     *  Steigt der Zaehler, verlangt die Zentrale Signaturen. Bleibt er bei
     *  0, obwohl ein Tastendruck nicht ankommt, liegt es NICHT an AES. */
    uint16_t aesChallenges() const {
        return m_aes_challenges;
    }

    /** Schluesselindex der zuletzt empfangenen Challenge, 0xFF = keine.
     *
     *  Der Funkindex ist das Doppelte der Schluesselnummer. 0 bedeutet
     *  also Schluesselnummer 0 = Auslieferungsschluessel; ein Wert > 0
     *  heisst, die Zentrale hat einen eigenen System-Sicherheits-
     *  schluessel und unser HM_DEF_KEY passt nicht. */
    uint8_t aesKeyIndex() const {
        return m_aes_keyindex;
    }

    /** Telegramme, die nicht in Message passten (len > buffersize()).
     *
     *  Jeder Treffer ist ein verlorenes Konfigurationstelegramm der
     *  Zentrale -- dann ist MaxDataLen zu klein gewaehlt. */
    uint16_t rxOversized() const {
        return m_rx_oversized;
    }

    /** Ausgang des letzten Sendevorgangs. Gehoert dem Radio-Thread. */
    TxResult txResult() const {
        return (TxResult)m_tx_result;
    }

    /** Vor einer Sendeaktion aufrufen, damit txResult() danach eindeutig zu
     *  genau dieser Aktion gehoert. */
    void clearTxResult() {
        m_tx_result = TxNone;
        m_tx_saw_aes = false;
    }

    /** true, wenn seit dem letzten clearTxResult() eine AES-Challenge kam.
     *
     *  Wird gebraucht, weil txResult() den Ausgang des LETZTEN
     *  Sendevorgangs traegt -- und das ist nach einer Challenge die
     *  Signaturantwort aus Device::processChallenge(), nicht mehr der
     *  Tastendruck. Ohne dieses Flag waere im UI nicht zu sehen, dass
     *  ueberhaupt signiert werden musste. */
    bool sawAesChallenge() const {
        return m_tx_saw_aes;
    }

    /** Zahl der erfolgreich abgestrahlten Telegramme.
     *
     *  Zusammen mit rxFrames() der schnellste Weg, einen fehlgeschlagenen
     *  Anlernvorgang einzuordnen, ohne am Log zu haengen:
     *    TX > 0, RX == 0  -> wir senden, hoeren aber niemanden. Entweder
     *                        erreicht uns die Zentrale nicht (Reichweite,
     *                        Sendeleistung) oder sie antwortet nicht.
     *    TX == 0          -> es geht gar nichts raus. Region-Sperre oder
     *                        Funk-Init gescheitert -- txResult() sagt was.
     *    RX > 0           -> der Funkweg steht in beide Richtungen; das
     *                        Problem liegt dann im Protokoll bzw. an der
     *                        Geraetekennung. */
    uint16_t txFrames() const {
        return m_tx_frames;
    }

    /** Zahl der gueltig empfangenen BidCos-Telegramme (CRC ok, >= 10 Byte),
     *  unabhaengig vom Adressaten. Zaehlt also allen BidCos-Funkverkehr in
     *  Reichweite, nicht nur die an uns gerichteten Telegramme. */
    uint16_t rxFrames() const {
        return m_rx_frames;
    }

    // -- Sendeabstand -------------------------------------------------------
    //
    //  ACHTUNG -- hier bewusst KEIN sysclock-Alarm wie in upstream.
    //  upstream/Radio.h::MinSendTimeout busy-wartet in waitTimeout() darauf,
    //  dass ein sysclock-Alarm feuert. Bei uns wird sysclock aber vom
    //  Radio-Thread getaktet -- genau dem Thread, der hier warten wuerde.
    //  Das waere ein garantierter Deadlock. Der Sendeabstand laeuft deshalb
    //  direkt ueber furi_get_tick().

    void setSendTimeout(uint16_t ms = ASKSIN_SEND_DELAY_MS);
    void waitTimeout(uint16_t ms);

    // -- Energie/Zustand ----------------------------------------------------

    void setIdle();
    void wakeup(bool flush = true);
    bool isIdle() const {
        return m_idle;
    }

    /** Frequenzkorrektur aus dem EEPROM (AskSin::config ruft das mit den
     *  CC1101-Registerwerten FREQ2/1/0 auf). */
    void tuneFreq(uint8_t freq2, uint8_t freq1, uint8_t freq0);

    // -- Von upstream/NoRadio erwartet, hier nur der Vollstaendigkeit halber -

    bool detectBurst();
    void flushrx();
    uint8_t getGDO0();
    uint8_t reset();
    uint8_t interruptMode() const {
        return 0;
    }
    bool clearAlive() {
        bool r = m_alive;
        m_alive = false;
        return r;
    }
    void initReg(uint8_t reg, uint8_t val) {
        // Einzelregister sind ueber subghz_devices_* nicht adressierbar.
        // Die Konfiguration kommt komplett aus dem Preset (siehe .cpp).
        UNUSED(reg);
        UNUSED(val);
    }

private:
    /** Blockiert, bis das Mindest-Sendeintervall abgelaufen ist. */
    void waitSendSlot();

    /** Wartet auf das Ende der laufenden Aussendung (GDO0-Flanken).
     *  @return false bei Timeout. */
    bool waitTxComplete(uint8_t payload_len);

    /** Wartet, bis ein gerade einlaufendes Telegramm vollstaendig im
     *  RX-FIFO liegt.
     *  @return true wenn der Empfaenger ruhig ist und der FIFO-Inhalt
     *          ausgewertet werden darf; false wenn abgebrochen wurde. */
    bool waitRxComplete();

    /** Den Empfaenger in groben Abstaenden neu scharf stellen. Details in
     *  der .cpp -- MCSM1 laesst den Chip nach JEDEM Paket nach IDLE laufen,
     *  auch nach einem, das der CRC-Autoflush stillschweigend verworfen
     *  hat. */
    void keepRxAlive();

    /** GDO0 als Eingang schalten. MUSS nach jedem furi_hal_subghz_reset()
     *  bzw. load_custom_preset() laufen -- die Firmware legt den Pin dort
     *  auf Analog zurueck. Details in der .cpp. */
    void enableGdo0();

    bool m_open;
    const GpioPin* m_gdo0;
    bool m_idle;
    bool m_alive;
    bool m_gdo0_seen;
    uint8_t m_rssi;

    /** Fruehester naechster Sendezeitpunkt als furi-Tick. */
    uint32_t m_next_send_tick;

    /** Naechster faelliger RX-Refresh als furi-Tick (siehe keepRxAlive). */
    uint32_t m_next_rearm_tick;

    // -- Quittungsverfolgung (siehe TxResult) -------------------------------
    uint8_t m_tx_result;
    uint8_t m_tx_count;
    bool m_tx_expect_ack;
    bool m_tx_saw_aes;
    HMID m_tx_to;

    // -- Diagnosezaehler ----------------------------------------------------
    uint16_t m_tx_frames;
    uint16_t m_rx_frames;
    uint16_t m_rx_crc_err;
    uint16_t m_rx_busy;
    uint16_t m_rx_oversized;
    uint16_t m_aes_challenges;
    uint8_t m_aes_keyindex;

    /** Arbeitspuffer fuer encode()/decode(). AskSin veraendert den Puffer
     *  in-place, deshalb darf nie direkt auf msg gearbeitet werden. */
    Message m_buf;
};

} // namespace as
