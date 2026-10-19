//- ---------------------------------------------------------------------------
//  Fassade zwischen Flipper-App und AskSin++
//
//  WARUM DIESE SCHICHT EXISTIERT (zwei unabhaengige Gruende):
//
//   1) Build: fbt kennt auf App() weder cflags noch wirksame cdefines fuer
//      Fremdcode -- beides gibt es nur auf Lib(). Wuerde eine Datei unter
//      src/ einen AskSin++-Header inkludieren, uebersetzte sie upstream OHNE
//      ARDUINO, ohne ASKSIN_FLIPPER_PORT und ohne STORAGEDRIVER, also in
//      einer voellig anderen Konfiguration als die Bibliothek. Das bricht
//      teils laut (Compilerfehler), teils still (abweichende Tickrate,
//      abweichendes Storage-Layout).
//
//   2) Architektur: AskSin++-Zustand gehoert exklusiv dem Radio-Thread
//      (CLAUDE.md §7.1). Eine schmale Fassade macht das erzwingbar statt
//      nur vereinbart -- src/ kann gar nicht an die Objekte heran.
//
//  Dieser Header inkludiert daher bewusst KEINEN AskSin++-Header und keinen
//  Arduino-Shim. Er ist reines POD + freie Funktionen.
//
//  Alle hm_service_*-Funktionen gehoeren dem Radio-Thread. Einzige Ausnahme
//  ist hm_service_status(), das unter Mutex kopiert und deshalb aus jedem
//  Thread aufgerufen werden darf.
//
//  Insbesondere hm_service_button() und hm_service_start_pairing() fassen
//  AskSin++-Objekte an und duerfen NICHT direkt aus dem App-Thread kommen --
//  der Weg dorthin fuehrt ueber die Kommandoschlange in
//  src/radio/hm_radio_thread.h.
//- ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Vorwaertsdeklaration des Flipper-Storage-Records.
struct Storage;

typedef enum {
    HmPairStateUnpaired = 0,
    HmPairStatePairing,
    HmPairStatePaired,
} HmPairState;

typedef enum {
    HmTxIdle = 0,
    HmTxSending,
    HmTxAck,
    HmTxNack,
    HmTxTimeout,
    HmTxBlocked, // von der Regionseinstellung der Firmware verweigert
    /** Die Zentrale hat eine AES-Challenge geschickt, die Signaturantwort
     *  wurde aber nicht quittiert.
     *
     *  Das ist der Zustand, der einen SCHLUESSELFEHLER von einem
     *  Funkfehler trennt: das Telegramm kam an (sonst gaebe es keine
     *  Challenge), nur die Signatur hat die Zentrale nicht akzeptiert --
     *  dann passt HM_DEF_KEY nicht zum System-Sicherheitsschluessel der
     *  CCU. Ist die Signatur in Ordnung, steht hier HmTxAck. */
    HmTxAesReq,
} HmTxResult;

/** Momentaufnahme fuer die GUI. Reines POD -- wird unter Mutex kopiert und
 *  danach ohne Lock gerendert (CLAUDE.md §7.2). */
typedef struct {
    HmPairState pair_state;
    HmTxResult tx_result; // Quittung zum letzten Sendevorgang
    uint8_t device_id[3]; // eigene HMID
    uint8_t master_id[3]; // HMID der Zentrale, 0 wenn ungepairt
    uint8_t last_channel; // zuletzt gesendeter Kanal 1..8, 0 = noch keiner
    bool last_longpress;
    uint8_t rssi_raw; // Pegelbetrag des letzten Empfangs in dBm, 0 = keiner
    bool radio_ok;
    // Diagnosezaehler. Ordnen einen fehlgeschlagenen Anlernvorgang ein,
    // ohne dass das Log gelesen werden muss -- Details an
    // FlipperRadio::txFrames().
    uint16_t tx_frames;
    uint16_t rx_frames;
    /** Zahl der empfangenen AES-Challenges seit dem Start. */
    uint16_t aes_challenges;
    /** Schluesselindex der letzten Challenge, 0xFF = noch keine.
     *  0 == Auslieferungsschluessel (der Funkindex ist das Doppelte der
     *  Schluesselnummer), > 0 == eigener Systemschluessel der Zentrale. */
    uint8_t aes_key_index;
} HmStatus;

/** Pegelbetrag in dBm. HmStatus::rssi_raw traegt die AskSin++-Skala, also
 *  den BETRAG des Pegels (60 == -60 dBm) und NICHT den CC1101-Rohwert --
 *  Begruendung an FlipperRadio::rssi(). */
static inline int hm_rssi_to_dbm(uint8_t raw) {
    return -(int)raw;
}

/** Funk, Storage und Systemuhr hochfahren.
 *  @param record offener Flipper-Storage-Record; bleibt im Besitz des Aufrufers.
 *  @return false wenn der CC1101 oder die Frequenz nicht verfuegbar ist. */
bool hm_service_init(struct Storage* record);

/** Gegenstueck zu hm_service_init(). Schreibt das EEPROM-Abbild zurueck. */
void hm_service_deinit(void);

/** Einen Schleifendurchlauf ausfuehren: Systemuhr takten, Alarme abarbeiten,
 *  Empfang pollen. Kehrt zuegig zurueck und blockiert nicht auf Funk. */
void hm_service_poll(void);

/** Empfohlene Wartezeit bis zum naechsten hm_service_poll() in ms. */
uint32_t hm_service_poll_interval_ms(void);

/** Tastenereignis.
 *
 *  Bildet die vier Zustände ab, die upstream/Remote.h::RemoteChannel::state()
 *  unterscheidet — ein einzelnes bool "longpress" reicht dafür nicht:
 *  ein langer Druck ist in BidCos eine *Folge* aus wiederholten
 *  KEY_EVENT_LONG-Telegrammen mit gleichem Zähler und einem abschließenden
 *  Release. Die CCU wertet das als PRESS_CONT/PRESS_LONG_RELEASE aus
 *  (siehe rf_rc.xml, paramset "key_valueset").
 *
 *  Abbildung auf die Flipper-GUI:
 *    InputTypeShort   -> HmButtonShort
 *    InputTypeLong    -> HmButtonLongStart
 *    InputTypeRepeat  -> HmButtonLongRepeat
 *    InputTypeRelease -> HmButtonLongEnd  (nur nach Long/Repeat)
 */
typedef enum {
    HmButtonShort = 0, // kurzer Druck  -> sendPeerEvent
    HmButtonLongStart, // Beginn lang   -> broadcastPeerEvent
    HmButtonLongRepeat, // gehalten     -> broadcastPeerEvent (gleicher Zähler)
    HmButtonLongEnd, // losgelassen     -> sendPeerEvent
} HmButtonEvent;

/** Tastenereignis absetzen.
 *  @param channel 1..8 */
void hm_service_button(uint8_t channel, HmButtonEvent ev);

/** Anlernmodus starten (entspricht der Config-Taste des Originalgeraets). */
void hm_service_start_pairing(void);

/** Aktuellen Zustand kopieren. Threadsicher. */
void hm_service_status(HmStatus* out);

/** EEPROM-Abbild schreiben, falls veraendert. Nicht aus dem Radio-Thread
 *  waehrend eines offenen Sendevorgangs aufrufen (CLAUDE.md §6.2). */
bool hm_service_flush(void);

#ifdef __cplusplus
}
#endif
