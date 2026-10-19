//- ---------------------------------------------------------------------------
//  AskSin++ Flipper-Port: zentrale Compile-Time-Konfiguration
//
//  Diese Datei MUSS vor jedem AskSin++-Header eingebunden werden. Sie wird
//  ueber port/asksin_flipper.h automatisch als erstes gezogen.
//- ---------------------------------------------------------------------------
#pragma once

// ---------------------------------------------------------------------------
//  Tickrate der AskSin++-Systemuhr
//
//  ACHTUNG: Nicht auf 1000 erhoehen. Alarm::tick ist ein 24-Bit-Bitfeld
//  (siehe upstream/Alarm.h:31). Bei 1000 Ticks/s waere der groesste
//  darstellbare Alarm ~4,6 h, und seconds2ticks(65535) -- wie es
//  BatterySensor und diverse Kanaeltimer verwenden -- laeuft still ueber.
//  100 Ticks/s (= 10 ms) ist der Upstream-Default; alle Timing-Konstanten
//  der Bibliothek sind darauf ausgelegt. BidCos braucht keine feinere
//  Aufloesung (ACK-Fenster ~200 ms).
// ---------------------------------------------------------------------------
#ifndef TICKS_PER_SECOND
#define TICKS_PER_SECOND 100UL
#endif

// Millisekunden pro AskSin++-Tick -- vom Sysclock-Pump verwendet.
#define ASKSIN_TICK_PERIOD_MS (1000UL / TICKS_PER_SECOND)

// ---------------------------------------------------------------------------
//  Kein RealTimeClock
//
//  RealTimeClock::init() in upstream/AlarmClock.h ist reines AVR-Timer2 und
//  faellt sonst in einen '#warning "RTC not supported"'-Zweig mit toten
//  getCounter()/getCurrentMillis(). Eine Fernbedienung braucht sie nicht.
// ---------------------------------------------------------------------------
#ifndef NORTC
#define NORTC
#endif

// ---------------------------------------------------------------------------
//  Storage
//
//  E2END ist bei AVR die letzte EEPROM-Adresse. upstream/Storage.h:247
//  rechnet size() = E2END + 1. Der Nicht-Arduino-Zweig definiert es nicht,
//  also setzen wir es passend zu unserem RAM-Shadow.
//  STORAGEDRIVER ersetzt InternalEprom komplett (upstream/Storage.h:703) --
//  dadurch ist fuer die Persistenz kein einziger Patch noetig.
// ---------------------------------------------------------------------------
#ifndef ASKSIN_EEPROM_SIZE
#define ASKSIN_EEPROM_SIZE 1024
#endif

#ifndef E2END
#define E2END (ASKSIN_EEPROM_SIZE - 1)
#endif

#ifndef STORAGEDRIVER
#define STORAGEDRIVER as::FlipperEeprom
#endif

// Dateiablage des EEPROM-Abbilds. APP_DATA_PATH("x") -> "/data/x", vom
// Storage-Service auf /ext/apps_data/<appid>/ gemappt.
#define ASKSIN_EEPROM_FILE    "eeprom.bin"
#define ASKSIN_EEPROM_TMPFILE "eeprom.bin.tmp"
// Optionale AES-Schluesseldatei. Fehlt sie, gilt HM_DEF_KEY aus
// application.fam. Format siehe FlipperEeprom::loadAesKey().
#define ASKSIN_AES_KEY_FILE   "aes_key.txt"
// Bytefolge auf Datei: 48 52 43 38 == "HRC8".
#define ASKSIN_EEPROM_MAGIC 0x38435248UL
// Version 2: Header traegt zusaetzlich HMID + Seriennummer (CLAUDE.md §6.4).
#define ASKSIN_EEPROM_FMT_VER 2

// Verzoegertes Zurueckschreiben (CLAUDE.md §6.2a). AskSin++ schreibt
// byteweise; ohne Entprellung ginge pro Konfigurationstelegramm ein
// kompletter Dateischreibvorgang auf die SD-Karte.
#define ASKSIN_EEPROM_FLUSH_DEBOUNCE_MS 1000

// ---------------------------------------------------------------------------
//  Funk
// ---------------------------------------------------------------------------
#define ASKSIN_FREQUENCY_HZ 868300000UL

// Name des internen CC1101 in der subghz_devices-Registry.
#define ASKSIN_SUBGHZ_DEVICE "cc1101_int"

// Minimaler Abstand zwischen zwei Sendevorgaengen in ms (Upstream-Default
// von Radio<> ist ebenfalls 100).
#define ASKSIN_SEND_DELAY_MS 100

// Obergrenze fuer ein BidCos-Rohtelegramm. Entspricht exakt der Klemmgrenze
// in cc1101_read_fifo() der Firmware (64 Byte).
#define ASKSIN_MAX_RAW_LEN 64

// Wie lange die UI nach dem Ausloesen im Zustand "Anlernen laeuft" bleibt.
// MultiChannelDevice::startPairing() sendet ein einzelnes DEVINFO und haelt
// das Geraet 20 s wach; danach ist der Vorgang gelaufen.
#define ASKSIN_PAIRING_TIMEOUT_MS 30000

// ---------------------------------------------------------------------------
//  Logging
// ---------------------------------------------------------------------------
#define ASKSIN_LOG_TAG "AskSin"

// Hex-Dumps kompletter Telegramme.
//
// DERZEIT AKTIV. Normalerweise aus (CLAUDE.md §8), aber ohne diese Dumps
// gibt es keinerlei Sicht auf den Funkverkehr: fbt uebersetzt FAPs mit
// -DNDEBUG, und upstream/Debug.h kompiliert dann saemtliche
// DPRINT/DHEX/DDEC zu leeren Makros. AskSin++ schweigt also vollstaendig,
// und der Serial-Shim in arduino_compat/ wird nie aufgerufen.
//
// Wer zusaetzlich die AskSin-Statemachine (Pairing, Config, Peers) sehen
// will, aktiviert in application.fam die auskommentierte "-UNDEBUG"-Zeile
// der Lib-cflags. Das ist ausdruecklich NUR zur Fehlersuche gedacht:
// DPRINT feuert dann auch mitten im ACK-Fenster (Device::send ruft
// msg.dump()) und verletzt damit §8.1.
//
// Vor dem produktiven Einsatz wieder auskommentieren -- Logging im
// Funk-Zeitfenster verzoegert im Millisekundenbereich.
#define HMRC8_TRACE_TELEGRAMS
