//- ---------------------------------------------------------------------------
//  FlipperEeprom -- RAM-Shadow + Persistenz auf die SD-Karte
//- ---------------------------------------------------------------------------
#include "flipper_eeprom.h"

#include <furi.h>
#include <furi_hal_version.h>
#include <storage/storage.h>

#include <stdio.h>
#include <string.h>

#define TAG ASKSIN_LOG_TAG

namespace as {

uint8_t FlipperEeprom::s_data[ASKSIN_EEPROM_SIZE];
bool FlipperEeprom::s_dirty = false;
uint32_t FlipperEeprom::s_dirty_tick = 0;
::Storage* FlipperEeprom::s_record = NULL;
AsksinIdentity FlipperEeprom::s_identity;

namespace {

/** Dateikopf, Format-Version 2.
 *
 *  Gegenueber Version 1 sind DeviceID und Serial dazugekommen. Sie stehen
 *  bewusst im Kopf und nicht im Shadow -- Begruendung an AsksinIdentity. */
struct __attribute__((packed)) EepromHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t reserved;
    uint16_t payload_size;
    uint8_t device_id[3];
    char serial[10];
    uint8_t reserved2[3];
    uint32_t crc; // ueber device_id + serial + Nutzdaten
};

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return crc;
}

uint32_t crc32(const uint8_t* data, size_t len) {
    return ~crc32_update(0xFFFFFFFFUL, data, len);
}

/** Pruefsumme ueber Identitaet UND Nutzdaten -- sonst koennte eine
 *  verfaelschte HMID unbemerkt durchlaufen. */
uint32_t payload_crc(const AsksinIdentity& id, const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    crc = crc32_update(crc, id.device_id, sizeof(id.device_id));
    crc = crc32_update(crc, (const uint8_t*)id.serial, sizeof(id.serial));
    crc = crc32_update(crc, data, len);
    return ~crc;
}

/** Geraetekennung beim Erststart erzeugen (CLAUDE.md §6.4).
 *
 *  Ableitung aus der STM32-UID des Flipper, nicht aus einem Zufallsgenerator.
 *  Zwei Gruende:
 *
 *   - Reproduzierbar. Geht eeprom.bin verloren (SD-Karte formatiert,
 *     Datei geloescht), bekommt das Geraet dieselbe HMID zurueck und die
 *     Zentrale erkennt es wieder, statt ein Duplikat anzulegen.
 *   - Eindeutig. Zwei Flipper im selben Homematic-Netz kollidieren nicht.
 *
 *  Trotzdem wird der Wert persistiert: so ist er im Dateikopf nachlesbar
 *  und liesse sich bei einer echten Kollision von Hand aendern. */
void generate_identity(AsksinIdentity& out) {
    const uint8_t* uid = furi_hal_version_uid();
    size_t uid_len = furi_hal_version_uid_size();

    // Zwei unabhaengige Hashes ueber dieselbe UID, getrennt durch ein
    // Praefix -- sonst waeren HMID und Seriennummer trivial ineinander
    // umrechenbar.
    uint32_t h_id = ~crc32_update(crc32_update(0xFFFFFFFFUL, (const uint8_t*)"HMID", 4), uid, uid_len);
    uint32_t h_ser = ~crc32_update(crc32_update(0xFFFFFFFFUL, (const uint8_t*)"SER", 3), uid, uid_len);

    out.device_id[0] = (uint8_t)(h_id >> 16);
    out.device_id[1] = (uint8_t)(h_id >> 8);
    out.device_id[2] = (uint8_t)(h_id);
    // 00 00 00 ist in BidCos die Broadcast-Adresse und als eigene HMID
    // unbrauchbar (HMID::valid() waere false).
    if(out.device_id[0] == 0 && out.device_id[1] == 0 && out.device_id[2] == 0) {
        out.device_id[2] = 0x01;
    }

    // eQ-3-Schema: drei Grossbuchstaben + sieben Ziffern, z. B. "JEQ0123456".
    // "FLP" macht das Geraet in der CCU-Liste sofort erkennbar.
    char buf[12];
    snprintf(buf, sizeof(buf), "FLP%07lu", (unsigned long)(h_ser % 10000000UL));
    memcpy(out.serial, buf, sizeof(out.serial));
}

} // namespace

// ---------------------------------------------------------------------------
//  Shadow-Zugriffe
//
//  Alle Adressen werden gegen die Shadow-Groesse geprueft. AskSin++ berechnet
//  Peer- und Registeradressen aus EEPROM-Inhalten; ein beschaedigtes Abbild
//  darf nicht in einen Speicherfehler laufen.
// ---------------------------------------------------------------------------

void FlipperEeprom::markDirty() {
    s_dirty = true;
    s_dirty_tick = furi_get_tick();
}

uint8_t FlipperEeprom::getByte(uint16_t addr) {
    if(addr >= ASKSIN_EEPROM_SIZE) {
        FURI_LOG_W(TAG, "eeprom read out of range: %u", addr);
        return 0xFF;
    }
    return s_data[addr];
}

bool FlipperEeprom::setByte(uint16_t addr, uint8_t d) {
    if(addr >= ASKSIN_EEPROM_SIZE) {
        FURI_LOG_W(TAG, "eeprom write out of range: %u", addr);
        return false;
    }
    if(s_data[addr] != d) {
        s_data[addr] = d;
        markDirty();
    }
    return true;
}

bool FlipperEeprom::getData(uint16_t addr, uint8_t* buf, uint16_t len) {
    if(buf == NULL) return false;
    if((uint32_t)addr + len > ASKSIN_EEPROM_SIZE) {
        FURI_LOG_W(TAG, "eeprom read block out of range: %u+%u", addr, len);
        memset(buf, 0xFF, len);
        return false;
    }
    memcpy(buf, &s_data[addr], len);
    return true;
}

bool FlipperEeprom::setData(uint16_t addr, uint8_t* buf, uint16_t len) {
    if(buf == NULL) return false;
    if((uint32_t)addr + len > ASKSIN_EEPROM_SIZE) {
        FURI_LOG_W(TAG, "eeprom write block out of range: %u+%u", addr, len);
        return false;
    }
    if(memcmp(&s_data[addr], buf, len) != 0) {
        memcpy(&s_data[addr], buf, len);
        markDirty();
    }
    return true;
}

bool FlipperEeprom::clearData(uint16_t addr, uint16_t len) {
    if((uint32_t)addr + len > ASKSIN_EEPROM_SIZE) {
        FURI_LOG_W(TAG, "eeprom clear out of range: %u+%u", addr, len);
        return false;
    }
    // Bewusst 0x00, nicht 0xFF: upstream/InternalEprom::clearData macht es
    // genauso, und StorageWrapper::reset() loescht darueber das Magic.
    for(uint16_t i = 0; i < len; i++) {
        if(s_data[addr + i] != 0) {
            s_data[addr + i] = 0;
            markDirty();
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Persistenz
// ---------------------------------------------------------------------------

void FlipperEeprom::bind(::Storage* record) {
    s_record = record;
}

namespace {

/** Eine Abbilddatei einlesen und pruefen.
 *  Schreibt bei Erfolg direkt in die uebergebenen Ziele.
 *  @return true wenn Kopf, Groesse und CRC stimmen. */
bool read_image(::Storage* record, const char* path, uint8_t* shadow, size_t shadow_len, AsksinIdentity& id) {
    File* file = storage_file_alloc(record);
    bool ok = false;

    do {
        if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            break;
        }

        EepromHeader hdr;
        if(storage_file_read(file, &hdr, sizeof(hdr)) != sizeof(hdr)) {
            FURI_LOG_W(TAG, "%s: header short read", path);
            break;
        }
        if(hdr.magic != ASKSIN_EEPROM_MAGIC) {
            FURI_LOG_W(TAG, "%s: magic mismatch 0x%08lX", path, (unsigned long)hdr.magic);
            break;
        }
        if(hdr.version != ASKSIN_EEPROM_FMT_VER) {
            FURI_LOG_W(TAG, "%s: format version %u not supported", path, hdr.version);
            break;
        }
        if(hdr.payload_size != ASKSIN_EEPROM_SIZE) {
            // Groesse geaendert -> Inhalt ist nicht mehr interpretierbar,
            // weil AskSin++ absolute Adressen speichert.
            FURI_LOG_W(TAG, "%s: size changed (%u != %u)", path, hdr.payload_size, ASKSIN_EEPROM_SIZE);
            break;
        }

        // Direkt in den Shadow lesen, NICHT ueber einen lokalen Puffer.
        // Ein uint8_t tmp[ASKSIN_EEPROM_SIZE] waere 1 KB auf dem Stack --
        // bei 4 KB Thread-Stack und storage_file_read() darunter ein
        // Stapelueberlauf mit Ansage. Bei CRC-Fehler wird der Shadow vom
        // Aufrufer ohnehin wieder mit 0xFF ueberschrieben.
        if(storage_file_read(file, shadow, shadow_len) != shadow_len) {
            FURI_LOG_W(TAG, "%s: payload short read", path);
            break;
        }

        AsksinIdentity fileid;
        memcpy(fileid.device_id, hdr.device_id, sizeof(fileid.device_id));
        memcpy(fileid.serial, hdr.serial, sizeof(fileid.serial));

        if(payload_crc(fileid, shadow, shadow_len) != hdr.crc) {
            FURI_LOG_W(TAG, "%s: crc mismatch", path);
            break;
        }
        // Eine HMID von 00 00 00 waere Broadcast -- als eigene Adresse
        // unbrauchbar. Lieber neu erzeugen als stumm kaputt weiterlaufen.
        if(fileid.device_id[0] == 0 && fileid.device_id[1] == 0 && fileid.device_id[2] == 0) {
            FURI_LOG_W(TAG, "%s: stored HMID is broadcast", path);
            break;
        }

        id = fileid;
        ok = true;
    } while(false);

    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

} // namespace

bool FlipperEeprom::load() {
    // Auslieferungszustand als Ausgangspunkt: ein ungeschriebenes AVR-EEPROM
    // liest 0xFF. StorageWrapper::setup() erkennt daran den Erststart.
    memset(s_data, 0xFF, sizeof(s_data));
    s_dirty = false;

    if(s_record == NULL) {
        FURI_LOG_E(TAG, "eeprom load without bound storage record");
        generate_identity(s_identity);
        return false;
    }

    bool ok = read_image(s_record, APP_DATA_PATH(ASKSIN_EEPROM_FILE), s_data, sizeof(s_data), s_identity);

    if(!ok) {
        // Rueckfallebene fuer das Fenster in flush(): zwischen dem Loeschen
        // der alten Datei und dem Umbenennen der neuen existiert nur die
        // .tmp. FatFS kann in f_rename kein vorhandenes Ziel ueberschreiben,
        // das Loeschen ist also unvermeidbar -- aber ein Stromausfall genau
        // dort darf kein angelerntes Geraet kosten.
        memset(s_data, 0xFF, sizeof(s_data));
        if(read_image(
               s_record, APP_DATA_PATH(ASKSIN_EEPROM_TMPFILE), s_data, sizeof(s_data), s_identity)) {
            FURI_LOG_W(TAG, "recovered eeprom image from interrupted write");
            ok = true;
            // Sofort wieder in den regulaeren Namen ueberfuehren.
            markDirty();
        }
    }

    if(!ok) {
        memset(s_data, 0xFF, sizeof(s_data));
        generate_identity(s_identity);
        // Damit die frisch erzeugte Identitaet auch ohne spaetere
        // Registeraenderung geschrieben wird.
        markDirty();
        FURI_LOG_I(TAG, "no usable eeprom image - starting factory fresh");
    } else {
        FURI_LOG_I(TAG, "eeprom image loaded (%u bytes)", (unsigned)sizeof(s_data));
    }

    char ser[11];
    memcpy(ser, s_identity.serial, 10);
    ser[10] = '\0';
    FURI_LOG_I(
        TAG,
        "identity: HMID %02X%02X%02X serial %s",
        s_identity.device_id[0],
        s_identity.device_id[1],
        s_identity.device_id[2],
        ser);

    return ok;
}

namespace {

/** Ein Hexzeichen in seinen Wert, oder 0xFF wenn es keins ist. */
uint8_t hex_nibble(char c) {
    if(c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if(c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if(c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0xFF;
}

} // namespace

bool FlipperEeprom::loadAesKey(uint8_t* key, uint8_t* index) {
    if(key == NULL || index == NULL || s_record == NULL) return false;

    File* file = storage_file_alloc(s_record);
    bool have_key = false;

    do {
        if(!storage_file_open(file, APP_DATA_PATH(ASKSIN_AES_KEY_FILE), FSAM_READ, FSOM_OPEN_EXISTING)) {
            // Normalfall -- ohne Datei gilt HM_DEF_KEY aus application.fam.
            break;
        }

        // Klein und fest: die Datei traegt zwei kurze Zeilen. Was darueber
        // hinausgeht, wird nicht gelesen.
        char buf[192];
        uint16_t n = storage_file_read(file, buf, sizeof(buf) - 1);
        buf[n] = '\0';

        // Zeilenweise zerlegen. Bewusst ohne strtok: das veraendert den
        // Puffer und ist nicht wiedereintrittsfaehig.
        uint16_t i = 0;
        while(i < n) {
            uint16_t start = i;
            while(i < n && buf[i] != '\n' && buf[i] != '\r') i++;
            uint16_t end = i;
            while(i < n && (buf[i] == '\n' || buf[i] == '\r')) i++;

            // Fuehrende Leerzeichen weg, Kommentare und Leerzeilen raus.
            while(start < end && (buf[start] == ' ' || buf[start] == '\t')) start++;
            if(start >= end || buf[start] == '#') continue;

            if(end - start > 4 && strncmp(buf + start, "key=", 4) == 0) {
                const char* p = buf + start + 4;
                const char* stop = buf + end;
                uint8_t bytes = 0;
                uint8_t hi = 0xFF;
                for(; p < stop && bytes < 16; p++) {
                    uint8_t v = hex_nibble(*p);
                    if(v == 0xFF) continue; // Trennzeichen zulassen
                    if(hi == 0xFF) {
                        hi = v;
                    } else {
                        key[bytes++] = (uint8_t)((hi << 4) | v);
                        hi = 0xFF;
                    }
                }
                // Nur ein vollstaendiger 128-Bit-Schluessel zaehlt. Eine
                // halbe Eingabe still zu akzeptieren waere schlimmer als
                // sie abzulehnen -- das Geraet wuerde dann mit einem
                // Fantasieschluessel signieren.
                if(bytes == 16 && hi == 0xFF) {
                    have_key = true;
                } else {
                    FURI_LOG_E(TAG, "aes_key.txt: key needs exactly 32 hex digits, got %u bytes", bytes);
                }
            } else if(end - start > 6 && strncmp(buf + start, "index=", 6) == 0) {
                uint32_t v = 0;
                bool any = false;
                for(uint16_t k = start + 6; k < end; k++) {
                    if(buf[k] < '0' || buf[k] > '9') break;
                    v = v * 10 + (uint32_t)(buf[k] - '0');
                    any = true;
                }
                if(any && v <= 0xFF) *index = (uint8_t)v;
            }
        }
    } while(false);

    storage_file_close(file);
    storage_file_free(file);
    return have_key;
}

bool FlipperEeprom::flush() {
    if(!s_dirty) return true;

    if(s_record == NULL) {
        FURI_LOG_E(TAG, "eeprom flush without bound storage record");
        return false;
    }

    EepromHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = ASKSIN_EEPROM_MAGIC;
    hdr.version = ASKSIN_EEPROM_FMT_VER;
    hdr.payload_size = ASKSIN_EEPROM_SIZE;
    memcpy(hdr.device_id, s_identity.device_id, sizeof(hdr.device_id));
    memcpy(hdr.serial, s_identity.serial, sizeof(hdr.serial));
    hdr.crc = payload_crc(s_identity, s_data, sizeof(s_data));

    File* file = storage_file_alloc(s_record);
    bool ok = false;

    do {
        // Erst nach tmp schreiben, dann umbenennen -- ein Stromausfall
        // mitten im Schreiben darf ein angelerntes Geraet nicht verlieren.
        storage_common_remove(s_record, APP_DATA_PATH(ASKSIN_EEPROM_TMPFILE));

        if(!storage_file_open(
               file, APP_DATA_PATH(ASKSIN_EEPROM_TMPFILE), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            FURI_LOG_E(TAG, "cannot open eeprom tmp file");
            break;
        }
        if(storage_file_write(file, &hdr, sizeof(hdr)) != sizeof(hdr)) {
            FURI_LOG_E(TAG, "eeprom header write failed");
            break;
        }
        if(storage_file_write(file, s_data, sizeof(s_data)) != sizeof(s_data)) {
            FURI_LOG_E(TAG, "eeprom payload write failed");
            break;
        }
        ok = true;
    } while(false);

    storage_file_close(file);
    storage_file_free(file);

    if(!ok) {
        storage_common_remove(s_record, APP_DATA_PATH(ASKSIN_EEPROM_TMPFILE));
        return false;
    }

    // Ab hier ist die .tmp vollstaendig und gueltig. Faellt der Strom im
    // Fenster bis zum rename aus, findet load() sie ueber die Rueckfallebene.
    storage_common_remove(s_record, APP_DATA_PATH(ASKSIN_EEPROM_FILE));
    FS_Error err = storage_common_rename(
        s_record, APP_DATA_PATH(ASKSIN_EEPROM_TMPFILE), APP_DATA_PATH(ASKSIN_EEPROM_FILE));
    if(err != FSE_OK) {
        FURI_LOG_E(TAG, "eeprom rename failed: %s", storage_error_get_desc(err));
        return false;
    }

    s_dirty = false;
    FURI_LOG_I(TAG, "eeprom image written");
    return true;
}

bool FlipperEeprom::flushIfDue(uint32_t debounce_ms) {
    if(!s_dirty) return false;
    // Differenzbildung, nicht Vergleich -- furi_get_tick() laeuft ueber
    // (CLAUDE.md §4.2).
    if((uint32_t)(furi_get_tick() - s_dirty_tick) < furi_ms_to_ticks(debounce_ms)) {
        return false;
    }
    return flush();
}

} // namespace as
