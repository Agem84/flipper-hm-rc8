//- ---------------------------------------------------------------------------
//  EEPROM-Ersatz fuer AskSin++ auf dem Flipper Zero
//
//  Wird ueber das Makro STORAGEDRIVER (flipper_config.h) anstelle von
//  upstream/InternalEprom eingesetzt -- upstream/Storage.h:703 sieht das
//  ausdruecklich vor, es ist also KEIN Patch noetig.
//
//  Kernidee (CLAUDE.md §6): AskSin++ schreibt byteweise. Wuerde jeder
//  setByte() auf die SD-Karte durchschlagen, waere sowohl das Timing als
//  auch die Karte hin. Alle Zugriffe laufen deshalb gegen einen statischen
//  RAM-Shadow; auf Datei geschrieben wird nur explizit ueber flush().
//
//  Die Daten sind static, nicht pro Instanz: AskSin++ legt Storage-Objekte
//  an mehreren Stellen an (AskSinBase::storage, __gb_store), und alle
//  muessen denselben Speicher sehen -- genau wie ein echtes EEPROM.
//- ---------------------------------------------------------------------------
#pragma once

#include "flipper_config.h"

#include <stdint.h>
#include <stddef.h>

// Vorwaertsdeklaration des Flipper-Storage-Records. Der volle Header wird
// nur in der .cpp gebraucht; so bleibt dieser Header leichtgewichtig und
// kollidiert nicht mit as::Storage.
struct Storage;

namespace as {

/** Dauerhafte Geraetekennung (CLAUDE.md §6.4, §9.2).
 *
 *  Liegt bewusst NICHT im AskSin-Shadow, sondern im Dateikopf: der Shadow
 *  wird von StorageWrapper::setup() verwaltet und bei jeder Aenderung des
 *  Registerlayouts (checksum-Mismatch) neu initialisiert. Die Identitaet
 *  muss das ueberleben, sonst taucht das Geraet nach jedem Umbau als neues
 *  Geraet in der Zentrale auf. */
struct AsksinIdentity {
    uint8_t device_id[3]; ///< HMID, nie 00 00 00 (das ist Broadcast)
    char serial[10]; ///< 10 Zeichen, NICHT nullterminiert
};

class FlipperEeprom {
public:
    FlipperEeprom() {}

    // -- von StorageWrapper/AskSin++ erwartete Schnittstelle ----------------

    bool present() {
        return true;
    }

    uint16_t size() {
        return (uint16_t)(E2END + 1);
    }

    // AskSin++ ruft store() an eigenen Zeitpunkten. Wir schreiben hier NICHT
    // sofort auf Datei -- das kann mitten im ACK-Fenster passieren
    // (CLAUDE.md §6.2). store() markiert nur; das echte Schreiben macht der
    // App-Thread ueber flush().
    void store() {
        markDirty();
    }

    uint8_t getByte(uint16_t addr);
    bool setByte(uint16_t addr, uint8_t d);
    bool setData(uint16_t addr, uint8_t* buf, uint16_t len);
    bool getData(uint16_t addr, uint8_t* buf, uint16_t len);
    bool clearData(uint16_t addr, uint16_t len);

    // -- Flipper-spezifisch -------------------------------------------------

    /** Storage-Record hinterlegen. Muss vor load()/flush() aufgerufen werden.
     *  Der Record gehoert weiterhin dem Aufrufer (App-Thread). */
    static void bind(::Storage* record);

    /** Abbild von Datei in den Shadow laden.
     *  Bei fehlender/beschaedigter Datei wird der Shadow mit 0xFF gefuellt --
     *  das ist der Auslieferungszustand eines AVR-EEPROMs, an dem
     *  StorageWrapper::setup() den Erststart erkennt.
     *
     *  Setzt in jedem Fall auch identity(): entweder aus der Datei oder,
     *  beim Erststart, aus der Hardware-UID des Flipper.
     *  @return true wenn ein gueltiges Abbild geladen wurde. */
    static bool load();

    /** Shadow auf Datei schreiben (tmp + rename).
     *  No-Op wenn nichts veraendert wurde.
     *  @return true bei Erfolg oder wenn nichts zu tun war. */
    static bool flush();

    /** flush(), aber erst wenn seit der letzten Aenderung mindestens
     *  debounce_ms vergangen sind (CLAUDE.md §6.2a).
     *  Nur an einem funkfreien Punkt aufrufen.
     *  @return true wenn tatsaechlich geschrieben wurde. */
    static bool flushIfDue(uint32_t debounce_ms);

    /** true, wenn seit dem letzten flush() geschrieben wurde. */
    static bool isDirty() {
        return s_dirty;
    }

    /** Persistente HMID und Seriennummer. Erst nach load() gueltig. */
    static const AsksinIdentity& identity() {
        return s_identity;
    }

    /** Optionale AES-Schluesseldatei einlesen.
     *
     *  Pfad: /ext/apps_data/hm_rc8/aes_key.txt (APP_DATA_PATH).
     *  Format, Zeilen in beliebiger Reihenfolge, '#' leitet einen
     *  Kommentar ein:
     *
     *      key=000102030405060708090a0b0c0d0e0f
     *      index=0
     *
     *  WOZU: HM_DEF_KEY ist ein Compile-Time-Makro (upstream/Sign.h:46),
     *  der eingebaute Wert ist aber nur der Beispielschluessel aus
     *  upstream/README.md und NICHT der verifizierte eQ-3-Werksschluessel
     *  (siehe application.fam). Ohne diesen Weg liesse sich ein anderer
     *  Schluessel nur durch einen Neubau ausprobieren -- und ein eigener
     *  System-Sicherheitsschluessel der Zentrale waere gar nicht
     *  nachtragbar.
     *
     *  Die Datei wird nur gelesen, nie geschrieben.
     *
     *  @param key   Ziel fuer 16 Byte, nur bei Rueckgabe true beschrieben.
     *  @param index Ziel fuer den Schluesselindex; bleibt unveraendert,
     *               wenn die Datei keine index-Zeile enthaelt.
     *  @return true wenn eine Datei mit gueltigem 16-Byte-Schluessel da war. */
    static bool loadAesKey(uint8_t* key, uint8_t* index);

    /** Direktzugriff auf den Shadow -- nur fuer Debug-Dumps. */
    static const uint8_t* shadow() {
        return s_data;
    }

private:
    /** s_dirty setzen und den Entprellzeitpunkt nachziehen. */
    static void markDirty();

    static uint8_t s_data[ASKSIN_EEPROM_SIZE];
    static bool s_dirty;
    static uint32_t s_dirty_tick;
    static ::Storage* s_record;
    static AsksinIdentity s_identity;
};

} // namespace as
