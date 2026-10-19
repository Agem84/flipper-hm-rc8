//- ---------------------------------------------------------------------------
//  Fassade Flipper-App <-> AskSin++ -- Implementierung
//
//  Geraet: 8-Tasten-Fernbedienung, die sich der Zentrale als HM-RC-X
//  ("Generic RC") zu erkennen gibt.
//
//  Warum HM-RC-X und nicht HM-RC-8:
//  Die Zielzentrale kennt kein HM-RC-8 (kein rf_rc_8.xml). rf_rc.xml bietet
//  stattdessen einen generischen Typ, der GENAU dafuer gemacht ist:
//
//    <type name="Generic RC" id="HM-RC-X" priority="1">
//      <parameter index="22.0" size="1.0" const_value="0x40"/></type>
//    <channel index="1" type="KEY" count_from_sysinfo="23.0:1.0" .../>
//
//  Index 22 des DEVINFO-Telegramms ist das DeviceType-Byte, Index 23 die
//  Kanalzahl. Beides bedient AskSin++ von selbst:
//    - DeviceType::Remote == 0x40            (upstream/Device.h:60)
//    - getDeviceInfo(): *di = this->channels() (upstream/Device.h:274)
//  Es ist also nichts von Hand zu setzen -- acht Kanaele im
//  MultiChannelDevice ergeben automatisch acht KEY-Kanaele in der CCU.
//
//  Der zweite generische Typ HM-RC-SB-X (rf_rc_single_on.xml) matcht auf
//  0x44 == DeviceType::SingleButton und kommt uns damit nicht in die Quere.
//- ---------------------------------------------------------------------------
#include "hm_service.h"

#include <asksin_flipper.h>

#include <furi.h>
#include <furi_hal_random.h>
#include <stdlib.h>
#include <string.h>

#define TAG "HmSvc"

// Bewusst KEIN 'using namespace as;': AskSin++ hat mit as::Storage einen
// eigenen Typ dieses Namens, der mit dem Flipper-Record ::Storage
// kollidiert. Ein using-Directive macht jede Nennung von 'Storage'
// mehrdeutig. Deshalb durchgaengig explizit qualifizieren.

namespace {

// Peers pro Kanal. Jeder Slot kostet EEPROM-Shadow; bei 8 Kanaelen und
// 1024 Byte ist 6 komfortabel. Der Upstream-Sketch nutzt 10 auf einem
// ATmega328 mit ebenfalls 1024 Byte -- Luft ist also da, falls mehr
// Verknuepfungen gebraucht werden.
const int PEERS_PER_CHANNEL = 6;

// Startadresse der Kanal-/Peerdaten im EEPROM (wie im Upstream-Sketch).
const uint16_t EEPROM_START_ADDR = 0x20;

typedef as::FlipperHal HalType;
typedef as::RemoteChannel<HalType, PEERS_PER_CHANNEL, as::List0> ChannelType;
typedef as::MultiChannelDevice<HalType, ChannelType, 8> RemoteType;

// ---------------------------------------------------------------------------
//  Geraetekennung
//
//  MODELL-ID 0xF001 -- bewusst weit ausserhalb der eQ-3-Vergabe.
//  Die Zentrale ordnet uns nur dann HM-RC-X (priority 1) zu, wenn die
//  Modell-ID KEINEM priority-2-Eintrag entspricht. Belegt sind dort im
//  Wesentlichen 0x0001..0x010C sowie 0x0640 und 0xC300.
//
//  Nicht in eine Luecke innerhalb dieses Blocks setzen: rf_rc_single_on.xml
//  enthaelt const_value="0x00065" -- FUENF Hex-Ziffern, ein Tippfehler in
//  eQ-3s eigenem XML. Solche Eintraege lassen Werte faelschlich frei
//  aussehen. Auch der Upstream-Beispielsketch HM-RC-8 nutzt 0x00DA, was in
//  dieser Firmware belegt ist -- deshalb wurde er hier nicht uebernommen.
//
//  DeviceID und Serial sind Platzhalter: sie werden in hm_service_init() aus
//  as::FlipperEeprom::identity() gefuellt, BEVOR g_dev konstruiert wird.
//  Device haelt nur eine 'const DeviceInfo&' und liest ueber
//  getDeviceID()/getDeviceSerial() jedes Mal daraus -- spaetere Aenderungen
//  waeren also sichtbar, aber die Registrierung an der Zentrale haengt
//  daran, deshalb steht der Wert vor dem Konstruktor fest.
//
//  Deshalb auch nicht 'const': das Objekt wird zur Laufzeit beschrieben.
//  PROGMEM ist auf ARM leer (§4.1), pgm_read_byte() darauf ein Direktzugriff.
// ---------------------------------------------------------------------------
struct as::DeviceInfo devinfo = {
    {0x00, 0x00, 0x00}, // HMID -- aus FlipperEeprom::identity()
    "??????????", // Seriennummer -- aus FlipperEeprom::identity()
    {0xF0, 0x01}, // Device Model -> faellt auf HM-RC-X durch
    0x01, // Firmware-Version
    as::DeviceType::Remote, // 0x40 -> Index 22
    {0x00, 0x00} // Info-Bytes; Kanalzahl schreibt AskSin++ selbst
};

// AskSin++ ist um globale Objekte gebaut. Auf dem Flipper laufen globale
// Konstruktoren im ELF-Loader-Kontext mit unklarer Reihenfolge relativ zum
// Furi-Start -- deshalb Arena + placement new (CLAUDE.md §3.1).
// Der Alias ist zusaetzlich noetig, weil '~as::FlipperHal()' keine gueltige
// Syntax ist und ohne using-Directive '~FlipperHal()' den Namen nicht findet.
alignas(HalType) uint8_t g_hal_arena[sizeof(HalType)];
HalType* g_hal = nullptr;

alignas(RemoteType) uint8_t g_dev_arena[sizeof(RemoteType)];
RemoteType* g_dev = nullptr;

as::FlipperSysClock g_clock;
FuriMutex* g_status_mutex = nullptr;
HmStatus g_status;
bool g_running = false;

/** Ende des UI-Anlernfensters als furi-Tick. 0 = kein Anlernen aktiv. */
uint32_t g_pairing_deadline = 0;

/** Bittet um ein sofortiges EEPROM-Schreiben am naechsten funkfreien Punkt.
 *  Gesetzt beim Uebergang nach "angelernt" (CLAUDE.md §6.2c). */
bool g_flush_requested = false;

/** Zeitpunkt der letzten Diagnosezeile. */
uint32_t g_diag_tick = 0;

/** Abstand der Diagnosezeile. 5 s ist selten genug, um das Log nicht zu
 *  fluten, und dicht genug, um einen Anlernversuch einordnen zu koennen. */
const uint32_t DIAG_INTERVAL_MS = 5000;

/** Eine Zeile mit allen Zaehlern, die einen fehlgeschlagenen Anlernvorgang
 *  einordnet -- ohne dass der komplette Funkverkehr mitgeschnitten werden
 *  muss. Laeuft am funkfreien Anfang von hm_service_poll() (CLAUDE.md §8.1).
 *
 *  Lesart:
 *    tx=0                -> es geht nichts raus. Region-Sperre oder
 *                           Funk-Init gescheitert.
 *    tx>0 rx=0           -> wir senden, hoeren aber niemanden.
 *    rx>0 busy=0 gdo0=0  -> GDO0 ist tot (Regel 5.2.1); der Empfang laeuft
 *                           blind und verliert Telegramme.
 *    oversz>0            -> Telegramme der Zentrale passen nicht in
 *                           Message -> MaxDataLen erhoehen.
 */
void log_diagnostics(bool force) {
    if(g_hal == nullptr) return;
    uint32_t now = furi_get_tick();
    if(!force && (uint32_t)(now - g_diag_tick) < furi_ms_to_ticks(DIAG_INTERVAL_MS)) return;
    g_diag_tick = now;

    as::FlipperRadio& r = g_hal->radio;
    if(!force && r.txFrames() == 0 && r.rxFrames() == 0 && r.rxBusyWaits() == 0) {
        // Noch kein Funkverkehr -- keine Zeile, die nichts aussagt.
        return;
    }

    FURI_LOG_I(
        TAG,
        "diag tx=%u rx=%u crcerr=%u busy=%u oversz=%u gdo0=%u rssi=-%udBm aes=%u keyidx=%u",
        r.txFrames(),
        r.rxFrames(),
        r.rxCrcErrors(),
        r.rxBusyWaits(),
        r.rxOversized(),
        r.gdo0Seen() ? 1u : 0u,
        r.rssi(),
        r.aesChallenges(),
        r.aesKeyIndex());
}

/** Pairing-/Master-Zustand aus dem Geraet in den Snapshot uebernehmen. */
void refresh_pairing_status() {
    if(g_dev == nullptr || g_status_mutex == nullptr) return;

    as::HMID master = g_dev->getMasterID();

    furi_mutex_acquire(g_status_mutex, FuriWaitForever);
    g_status.master_id[0] = master.id0();
    g_status.master_id[1] = master.id1();
    g_status.master_id[2] = master.id2();

    HmPairState before = g_status.pair_state;

    if(master.valid()) {
        g_status.pair_state = HmPairStatePaired;
    } else if(g_status.pair_state == HmPairStatePairing) {
        // Anlernfenster begrenzen. startPairing() sendet ein einzelnes
        // DEVINFO; antwortet die Zentrale nicht, stand die Anzeige sonst bis
        // zum App-Ende auf "Anlernen laeuft".
        if((int32_t)(g_pairing_deadline - furi_get_tick()) <= 0) {
            FURI_LOG_I(TAG, "pairing window expired");
            g_status.pair_state = HmPairStateUnpaired;
            g_pairing_deadline = 0;
        }
    } else {
        g_status.pair_state = HmPairStateUnpaired;
    }

    bool just_paired = (before != HmPairStatePaired && g_status.pair_state == HmPairStatePaired);
    furi_mutex_release(g_status_mutex);

    if(just_paired) {
        // Nicht hier schreiben -- wir stehen direkt hinter pollRadio() und
        // damit potenziell in einem Funkfenster (CLAUDE.md §6.2). Der
        // naechste Schleifendurchlauf erledigt es an ruhiger Stelle.
        FURI_LOG_I(
            TAG,
            "paired with central %02X%02X%02X - eeprom flush requested",
            master.id0(),
            master.id1(),
            master.id2());
        g_pairing_deadline = 0;
        g_flush_requested = true;
    }
}

/** Ausgang des letzten Sendevorgangs vom Radio in den Snapshot uebernehmen. */
void publish_tx_result() {
    HmTxResult r;
    switch(g_hal->radio.txResult()) {
    case as::FlipperRadio::TxAck:
        r = HmTxAck;
        break;
    case as::FlipperRadio::TxNack:
        r = HmTxNack;
        break;
    case as::FlipperRadio::TxBlocked:
        r = HmTxBlocked;
        break;
    case as::FlipperRadio::TxAesChallenge:
        // Challenge empfangen, aber danach kam keine Quittung mehr --
        // Device::processChallenge() hat entweder gar nicht signiert oder
        // die Zentrale hat die Signatur verworfen.
        r = HmTxAesReq;
        break;
    case as::FlipperRadio::TxPending:
        // Gesendet, ACK erwartet, keins gekommen -- Device::send() hat alle
        // Versuche aus transmitDevTryMax() verbraucht.
        r = HmTxTimeout;
        break;
    case as::FlipperRadio::TxSent:
        // Broadcast oder Telegramm ohne BIDI -- es gibt nichts zu quittieren.
        r = HmTxIdle;
        break;
    default:
        r = HmTxIdle;
        break;
    }

    // Nach einer Challenge traegt txResult() den Ausgang der
    // SIGNATURANTWORT, nicht mehr den des Tastendrucks. Ein ACK heisst
    // dann "Signatur akzeptiert" und bleibt stehen. Alles andere wird auf
    // HmTxAesReq zurueckgebogen, damit im UI nicht "T/O" steht, wo in
    // Wahrheit die Signatur abgelehnt wurde.
    if(r != HmTxAck && g_hal->radio.sawAesChallenge()) {
        r = HmTxAesReq;
    }

    furi_mutex_acquire(g_status_mutex, FuriWaitForever);
    g_status.tx_result = r;
    g_status.rssi_raw = g_hal->radio.rssi();
    g_status.tx_frames = g_hal->radio.txFrames();
    g_status.rx_frames = g_hal->radio.rxFrames();
    g_status.aes_challenges = g_hal->radio.aesChallenges();
    g_status.aes_key_index = g_hal->radio.aesKeyIndex();
    furi_mutex_release(g_status_mutex);
}

} // namespace

bool hm_service_init(::Storage* record) {
    if(g_running) return true;

    memset(&g_status, 0, sizeof(g_status));
    g_status.pair_state = HmPairStateUnpaired;
    g_status.tx_result = HmTxIdle;
    // 0 waere ein gueltiger Index (Auslieferungsschluessel) -- 0xFF heisst
    // "noch keine Challenge gesehen".
    g_status.aes_key_index = 0xFF;
    // 0 heisst "noch nichts gesendet" -- die UI blendet die Ereignisanzeige
    // dann aus, statt einen Tastendruck vorzutaeuschen.
    g_status.last_channel = 0;

    g_pairing_deadline = 0;
    g_flush_requested = false;
    g_diag_tick = furi_get_tick();

    g_status_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(g_status_mutex == nullptr) return false;

    // Kein mkdir noetig und auch nicht moeglich: APP_DATA_PATH("") ergaebe
    // "/data/" -- ein Pfad mit leerem Namen, den die Storage-API ablehnt.
    // Das Verzeichnis /ext/apps_data/<appid>/ legt der Storage-Service beim
    // Aufloesen des /data-Alias selbst an.
    as::FlipperEeprom::bind(record);
    bool restored = as::FlipperEeprom::load();
    FURI_LOG_I(TAG, "eeprom: %s", restored ? "restored" : "factory fresh");

    // MUSS vor der Konstruktion von g_dev stehen: der Konstruktor legt
    // ueber kstore/list0 das Speicherlayout an, und Device haelt danach nur
    // noch eine Referenz auf devinfo.
    const as::AsksinIdentity& ident = as::FlipperEeprom::identity();
    memcpy(devinfo.DeviceID, ident.device_id, sizeof(devinfo.DeviceID));
    memcpy(devinfo.Serial, ident.serial, 10);
    devinfo.Serial[10] = '\0';

    g_hal = new(g_hal_arena) HalType();

    // Funk zuerst separat hochfahren, damit ein Fehler sichtbar wird.
    // Device::init() ruft radio.init() erneut -- FlipperRadio::init() ist
    // dafuer idempotent gemacht.
    if(!g_hal->radio.init()) {
        FURI_LOG_E(TAG, "radio init failed");
        g_hal->~HalType();
        g_hal = nullptr;
        furi_mutex_free(g_status_mutex);
        g_status_mutex = nullptr;
        return false;
    }

    g_dev = new(g_dev_arena) RemoteType(devinfo, EEPROM_START_ADDR);

    // init() zieht HMID/Serial aus devinfo, initialisiert Storage und ruft
    // hal.init(id). initDone() stoesst die Konfigurationsverteilung an alle
    // Kanaele an.
    g_dev->init(*g_hal);

    // Zufallsquelle fuer die AES-Nonce. upstream/Device.h::processChallenge()
    // fuellt die ersten sechs Byte der Signaturantwort mit rand(). Ohne
    // srand() ist diese Folge nach jedem Neustart identisch -- fuer ein
    // Challenge-Response-Verfahren ist eine vorhersagbare Nonce der falsche
    // Ausgangspunkt. furi_hal_random_get() ist der Hardware-RNG des STM32WB
    // und in api_symbols.csv mit '+' eingetragen.
    srand((unsigned int)furi_hal_random_get());

    // Optionalen Schluessel aus der Datei uebernehmen. Muss NACH init()
    // laufen: dort legt firstinit() ueber keystore().defaults() erst den
    // eingebauten HM_DEF_KEY ab, den wir hier gezielt ersetzen.
    {
        uint8_t key[16];
        uint8_t idx = g_dev->keystore().getIndex();
        if(as::FlipperEeprom::loadAesKey(key, &idx)) {
            g_dev->keystore().writeKey(key);
            g_dev->keystore().setIndex(idx);
            FURI_LOG_I(TAG, "aes key loaded from %s, key index %u", ASKSIN_AES_KEY_FILE, idx);
        } else {
            FURI_LOG_I(
                TAG,
                "aes: using built-in HM_DEF_KEY, key index %u (unverified, see application.fam)",
                g_dev->keystore().getIndex());
        }
    }

    g_dev->initDone();

    g_clock.begin();

    g_status.radio_ok = true;
    g_status.device_id[0] = devinfo.DeviceID[0];
    g_status.device_id[1] = devinfo.DeviceID[1];
    g_status.device_id[2] = devinfo.DeviceID[2];

    g_running = true;
    refresh_pairing_status();

    // Erststart legt Register und Defaults an -> gleich sichern.
    as::FlipperEeprom::flush();

    // Belegung des EEPROM-Shadows. Mit USE_AES kommen 17 Byte KeyStore
    // hinzu und ALLES dahinter verschiebt sich; die Zahlen zeigen, ob das
    // Layout noch in ASKSIN_EEPROM_SIZE passt. upstream loggt das in
    // MultiChannelDevice::dumpSize() nur per DPRINT, und das ist unter
    // -DNDEBUG tot (CLAUDE.md §8).
    FURI_LOG_I(
        TAG,
        "eeprom layout: keystore@%u .. free@%u of %u bytes",
        g_dev->keystore().address(),
        g_dev->getUserStorage().getAddress(),
        (unsigned)ASKSIN_EEPROM_SIZE);

    FURI_LOG_I(
        TAG,
        "service up: %u channels, model %02X%02X, HMID %02X%02X%02X",
        g_dev->channels(),
        devinfo.DeviceModel[0],
        devinfo.DeviceModel[1],
        devinfo.DeviceID[0],
        devinfo.DeviceID[1],
        devinfo.DeviceID[2]);
    return true;
}

void hm_service_deinit(void) {
    if(!g_running) return;
    g_running = false;

    g_pairing_deadline = 0;
    g_flush_requested = false;
    as::FlipperEeprom::flush();

    if(g_dev != nullptr) {
        g_dev->~RemoteType();
        g_dev = nullptr;
    }

    if(g_hal != nullptr) {
        g_hal->radio.deinit();
        g_hal->~HalType();
        g_hal = nullptr;
    }

    as::FlipperEeprom::bind(nullptr);

    if(g_status_mutex != nullptr) {
        furi_mutex_free(g_status_mutex);
        g_status_mutex = nullptr;
    }
    FURI_LOG_I(TAG, "service down");
}

void hm_service_poll(void) {
    if(!g_running || g_hal == nullptr || g_dev == nullptr) return;

    // Persistenz und Diagnoseausgabe ganz am Anfang -- das ist der einzige
    // garantiert funkfreie Punkt der Schleife (CLAUDE.md §6.2 und §8.1).
    // hm_service_button() ist bereits vollstaendig abgearbeitet, wenn wir
    // hier ankommen: Device::send() blockiert bis zum ACK oder Timeout.
    // Nach pollRadio() waere es das nicht mehr.
    //
    // drain() gibt alles aus, was write()/read() waehrend des Sendens und
    // des ACK-Fensters in den Ringpuffer geschrieben haben. Direkt dort zu
    // loggen wuerde die BidCos-Timings zerstoeren.
    as::RadioTrace::drain();
    log_diagnostics(false);

    if(g_flush_requested) {
        g_flush_requested = false;
        as::FlipperEeprom::flush();
    } else {
        as::FlipperEeprom::flushIfDue(ASKSIN_EEPROM_FLUSH_DEBOUNCE_MS);
    }

    // Reihenfolge zaehlt: erst die Uhr nachziehen, damit runready() die
    // faelligen Alarme dieses Durchlaufs sieht.
    g_clock.pump();
    g_hal->runready();

    // Uebernimmt Empfang, Dekodierung, ACK-Behandlung und die komplette
    // BidCos-Statemachine (Pairing, Config, Peers).
    g_dev->pollRadio();

    refresh_pairing_status();

    // RSSI und Diagnosezaehler nachfuehren, auch wenn wir selbst nichts
    // gesendet haben -- empfangene Telegramme aktualisieren beides.
    furi_mutex_acquire(g_status_mutex, FuriWaitForever);
    g_status.rssi_raw = g_hal->radio.rssi();
    g_status.tx_frames = g_hal->radio.txFrames();
    g_status.rx_frames = g_hal->radio.rxFrames();
    g_status.aes_challenges = g_hal->radio.aesChallenges();
    g_status.aes_key_index = g_hal->radio.aesKeyIndex();
    furi_mutex_release(g_status_mutex);
}

uint32_t hm_service_poll_interval_ms(void) {
    // Entspricht der AskSin-Tickperiode (TICKS_PER_SECOND = 100).
    //
    // Frueher standen hier 2 ms. Das waren 500 Durchlaeufe pro Sekunde, und
    // jeder davon macht mit rx_pipe_not_empty() eine SPI-Transaktion. Der
    // CC1101 haengt am selben internen SPI-Bus wie das Display -- der
    // GUI-Service kam dadurch nicht mehr an den Bus und die Firmware meldete
    // "ViewPort lockup".
    //
    // 10 ms sind fuer BidCos reichlich: das Paket liegt im RX-FIFO und
    // laeuft nicht weg, ein Telegramm dauert bei ~10 kBaud rund 40 ms, und
    // das ACK-Fenster ist ~200 ms breit.
    return 10;
}

void hm_service_button(uint8_t channel, HmButtonEvent ev) {
    if(!g_running || g_dev == nullptr) return;
    if(channel < 1 || channel > g_dev->channels()) {
        FURI_LOG_W(TAG, "button on invalid channel %u", channel);
        return;
    }

    // RemoteChannel::state() baut das Telegramm selbst und entscheidet
    // zwischen sendPeerEvent (released/longreleased) und
    // broadcastPeerEvent (longpressed) -- siehe upstream/Remote.h:61.
    uint8_t s;
    switch(ev) {
    case HmButtonShort:
        s = as::Button::released;
        break;
    case HmButtonLongStart:
    case HmButtonLongRepeat:
        s = as::Button::longpressed;
        break;
    case HmButtonLongEnd:
        s = as::Button::longreleased;
        break;
    default:
        return;
    }

    furi_mutex_acquire(g_status_mutex, FuriWaitForever);
    g_status.last_channel = channel;
    g_status.last_longpress = (ev != HmButtonShort);
    g_status.tx_result = HmTxSending;
    furi_mutex_release(g_status_mutex);

    // state() blockiert bis Device::send() fertig ist -- inklusive aller
    // Wiederholungen und ACK-Fenster, in der Spitze rund zwei Sekunden.
    // Solange steht die Anzeige auf HmTxSending; der App-Thread liest den
    // Snapshot weiter alle 100 ms.
    g_hal->radio.clearTxResult();
    g_dev->channel(channel).state(s);
    publish_tx_result();

    // Der Sendevorgang ist abgeschlossen, die Schleife wieder funkfrei --
    // erst jetzt darf der Mitschnitt raus (CLAUDE.md §8.1).
    as::RadioTrace::drain();
    FURI_LOG_I(
        TAG,
        "button ch%u ev=%u -> tx_result=%u repeat=%u",
        channel,
        (unsigned)ev,
        (unsigned)g_hal->radio.txResult(),
        g_dev->channel(channel).repeatCount());
    log_diagnostics(true);
}

void hm_service_start_pairing(void) {
    if(!g_running || g_dev == nullptr) return;

    furi_mutex_acquire(g_status_mutex, FuriWaitForever);
    g_status.pair_state = HmPairStatePairing;
    g_status.tx_result = HmTxSending;
    furi_mutex_release(g_status_mutex);

    g_pairing_deadline = furi_get_tick() + furi_ms_to_ticks(ASKSIN_PAIRING_TIMEOUT_MS);

    // Vollstaendige Kennung ins Log, bevor gesendet wird. Genau diese Werte
    // muss die Zentrale im Anlernmodus sehen; weichen sie von dem ab, was
    // dort auftaucht, liegt der Fehler nicht am Funk (CLAUDE.md §9.1).
    char serial[11];
    memcpy(serial, devinfo.Serial, 10);
    serial[10] = '\0';
    FURI_LOG_I(
        TAG,
        "start pairing: HMID %02X%02X%02X serial %s model %02X%02X fw %02X type %02X ch %u",
        devinfo.DeviceID[0],
        devinfo.DeviceID[1],
        devinfo.DeviceID[2],
        serial,
        devinfo.DeviceModel[0],
        devinfo.DeviceModel[1],
        devinfo.Firmware,
        devinfo.DeviceType,
        g_dev->channels());

    g_hal->radio.clearTxResult();
    g_dev->startPairing();
    // Gerade hier wichtig: eine Regionssperre faellt beim Anlernen zuerst
    // auf, weil es die erste bewusste Aussendung ueberhaupt ist.
    publish_tx_result();

    // Das DEVINFO ist raus -- jetzt ist die Schleife wieder funkfrei und der
    // Mitschnitt des Sendevorgangs darf ins Log.
    as::RadioTrace::drain();
    log_diagnostics(true);
}

void hm_service_status(HmStatus* out) {
    if(out == NULL) return;
    if(g_status_mutex == nullptr) {
        memset(out, 0, sizeof(*out));
        return;
    }
    // Nur kopieren, nicht rendern -- der Aufrufer arbeitet ohne Lock weiter.
    furi_mutex_acquire(g_status_mutex, FuriWaitForever);
    memcpy(out, &g_status, sizeof(*out));
    furi_mutex_release(g_status_mutex);
}

bool hm_service_flush(void) {
    return as::FlipperEeprom::flush();
}
