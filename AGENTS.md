# AGENTS.md — Flipper Zero FAP: Homematic BidCos Remote (AskSin++)

Regelwerk für dieses Projekt. Diese Datei hat Vorrang vor generischen Defaults.
Wenn eine Regel hier mit einer Gewohnheit aus Arduino-/AVR-Welt kollidiert: **diese Datei gewinnt.**

---

## 0. Projektziel & Scope

Native Flipper-Zero-App (FAP), die den **internen CC1101** nutzt, um über eine portierte
**AskSin++**-Bibliothek als Homematic-BidCos-Gerät mit einer CCU/RaspberryMatic zu sprechen.

**Meilenstein 1 (aktueller Scope):** Emulation einer 8-Tasten-Fernbedienung (HM-RC-8-Klasse).
Das Gerät muss sich regulär über den Anlernmodus der Zentrale anlernen lassen und danach
Tastendrücke (short/long) als BidCos-Telegramme senden, inkl. korrekter ACK-Behandlung
und persistenter Peer-/Register-Daten über Neustarts hinweg.

**Explizit nicht in Scope (nicht vorwegnehmen, nicht "schon mal einbauen"):**
Sensor-/Aktorkanäle, OTA-Firmwareupdate, externe CC1101-Module, AES-Signing,
Multi-Device-Emulation, SubGhz-Replay-Funktionen.

**Verifizierte Umgebung (Stand 2026-08-14):**

| Komponente | Wert |
|---|---|
| Build-Tool | `ufbt` (Python-Wheel, in `PATH`) |
| SDK | `~/.ufbt/current`, Target `f7`, Firmware `1.4.3` |
| API-Version | `87.1` (`sdk_headers/f7_sdk/targets/f7/api_symbols.csv`) |
| Toolchain | `arm-none-eabi-gcc` aus dem ufbt-Toolchain-Cache |

---

## 1. Verbindliche Quelle für Firmware-APIs

**Regel 1.1 — Kein API-Rating aus dem Gedächtnis.**
Bevor eine `furi_*`, `furi_hal_*`, `subghz_devices_*`, `storage_*` oder `gui`-Funktion
im Code verwendet wird, ist sie gegen die lokale Symboltabelle zu prüfen:

```bash
rg "furi_hal_subghz" "$HOME/.ufbt/current/sdk_headers/f7_sdk/targets/f7/api_symbols.csv"
```

Spalte 2 ist entscheidend: `+` = exportiert und aus dem FAP aufrufbar, `-` = **nicht**
aufrufbar (Linker-/Loader-Fehler zur Laufzeit, nicht zur Compile-Zeit!).

**Regel 1.2 — Bekannte Fallen in dieser SDK-Version:**

* `furi_hal_subghz_init()` ist mit `-` markiert → **darf nicht aufgerufen werden.**
  Initialisierung ausschließlich über `subghz_devices_begin()`.
* `furi_hal_power_insomnia_level()` ist `-`; `furi_hal_power_insomnia_enter/exit()` sind `+`.
* `furi_hal_region_init()` ist `-`; `furi_hal_region_is_frequency_allowed()` ist `+`.

**Regel 1.3 — Header lesen, nicht raten.** Signaturen und Enums stammen aus
`~/.ufbt/current/sdk_headers/f7_sdk/…`. Bei Unsicherheit über ein Datenformat
(z. B. Custom-Preset-Layout) wird das im Code als `// TODO(verify):` markiert und
auf Hardware verifiziert — **nicht** stillschweigend angenommen.

---

## 2. Build-System (`application.fam` / ufbt)

### 2.1 Korrektur zur Prämisse: es gibt kein `fap_cxx`

`fbt`/`ufbt` haben **kein** `fap_cxx`-Flag. C++ wird allein dadurch aktiviert, dass
`.cpp`-Dateien im `sources`-Glob liegen; der SCons-Builder wählt den C++-Frontend
automatisch. Was tatsächlich zu beachten ist:

* Es wird mit **`-fno-exceptions -fno-rtti`** gebaut. `try/catch`, `throw`,
  `dynamic_cast`, `typeid` sind nicht verfügbar. Kein `std::` mit Exception-Pfad
  (`std::vector`, `std::string`, `std::function` → **verboten**, siehe §3).
* Pro-App-Compilerflags gibt es in `App()` nicht. Wer eigene Flags braucht
  (z. B. `-Wno-*` für Fremdcode), muss den Code in ein `Lib()` unter `lib/<name>/`
  auslagern — dort existiert `cflags`. **Genau deshalb liegt AskSin++ als privates Lib vor.**
* Der Entry-Point muss `extern "C"` sein:
  ```cpp
  extern "C" int32_t hm_rc8_app(void* p);
  ```

### 2.2 Verzeichnislayout (verbindlich)

```
Flipper/
├── application.fam            # FAP-Manifest
├── AGENTS.md                  # diese Datei
├── icons/
│   └── hm_rc8_10px.png        # 10x10 px, 1-bit — Build bricht ohne diese Datei ab
├── assets_src/                # Quellbilder (nicht im Build)
├── src/                       # REINER Flipper-Code — kein AskSin++, siehe §2.2.2
│   ├── hm_rc8_app.cpp         # Entry-Point, App-Lifecycle, Records
│   ├── ui/                    # ViewPort, Draw-/Input-Callbacks, Modell-Snapshot
│   └── radio/                 # Radio-Thread, Kommando-Queue
└── lib/
    └── asksinpp/
        ├── upstream/          # pa-pa/AskSin++, Vendor-Kopie (git clone)
        ├── patches/           # nummerierte *.patch gegen upstream/
        └── port/              # alles, was AskSin++ sehen darf
            ├── flipper_config.h       # zentrale Compile-Time-Schalter
            ├── asksin_flipper.h       # Sammel-Header, erzwingt Include-Reihenfolge
            ├── arduino_compat/        # Arduino.h-Ersatz + Serial→FURI_LOG
            ├── flipper_eeprom.*       # STORAGEDRIVER: RAM-Shadow + Datei
            ├── flipper_radio.*        # CC1101 über subghz_devices_*
            ├── flipper_sysclock.h     # Taktgeber für as::sysclock
            ├── flipper_led.h          # Status-LED über furi_hal_light
            ├── cxx_runtime.cpp        # operator new/delete, __cxa_pure_virtual
            ├── hm_service.h           # FASSADE — einziger Zugang für src/
            └── hm_service.cpp         # Geräte-/Kanallogik (AskSin++-Seite)
```

**Regel 2.2.2 — Unter `src/` steht niemals ein AskSin++- oder Arduino-Include.**
Der gesamte Kontakt läuft über `lib/asksinpp/port/hm_service.h`, einen reinen
POD-/C-Header. Zwei unabhängige Gründe:

* **Build:** `App()` kennt weder `cflags` noch wirksame `cdefines` für
  Fremdcode — beides gibt es nur auf `Lib()`. Eine Datei unter `src/`, die
  `AskSinPP.h` zieht, übersetzt upstream **ohne** `ARDUINO`, ohne
  `ASKSIN_FLIPPER_PORT` und ohne `STORAGEDRIVER`, also in einer völlig anderen
  Konfiguration als die Bibliothek. Das bricht teils laut, teils still
  (abweichende Tickrate, abweichendes Storage-Layout).
* **Architektur:** So ist §7.1 erzwungen statt nur vereinbart — `src/` kommt an
  die AskSin++-Objekte gar nicht heran.

Neuer Code, der AskSin++ berührt, gehört deshalb nach `port/`, nicht nach
`src/device/`. Das weicht bewusst von der naheliegenden Aufteilung ab.

**Regel 2.2.0 — Source-Globs sind immer explizit.** `fbt` verwendet `GlobRecursive`:
das Pattern wird in *jedem* Unterverzeichnis ab dem App-Root angewandt. Der Default
`["*.c*"]` würde `lib/asksinpp/upstream/examples/` mit einsammeln. Deshalb stehen in
`sources` ausschließlich verzeichnisgebundene Patterns (`src/ui/*.cpp` statt `*.cpp`).

**Regel 2.2.1 — `upstream/` wird nie direkt editiert.** Jede nötige Änderung an
AskSin++ ist ein Patch in `lib/asksinpp/patches/NNN-kurzbeschreibung.patch` mit
Kommentarkopf (*warum*). Grund: die Bibliothek soll auf neue Upstream-Stände
rebasebar bleiben. Anpassungen, die sich als Shim in `port/` lösen lassen,
**gehören nach `port/`, nicht in einen Patch.**

### 2.3 Sonstige Manifest-Regeln

* `apptype=FlipperAppType.EXTERNAL`, `fap_category="Sub-GHz"`.
* `stack_size` gilt nur für den **App-Thread**. Der Radio-Thread bekommt seinen
  eigenen Stack in `furi_thread_alloc_ex(...)`. Beide Werte sind mit
  `furi_thread_get_stack_space()` im Debug-Build zu messen und zu dokumentieren,
  nicht zu raten.
* `subghz_devices_*` steht in `api_symbols.csv` → wird dynamisch vom Loader
  aufgelöst. **Kein `fap_libs=["subghz"]` nötig.** Nicht "vorsichtshalber" hinzufügen.
* Build/Deploy: `ufbt` (bauen), `ufbt launch` (flashen + starten), `ufbt cli` (Log),
  `ufbt faps` / `ufbt vscode_dist` bei Bedarf.

---

## 3. C++-Regeln in der FAP-Umgebung

**Regel 3.1 — Keine statischen Konstruktoren mit Nebenwirkungen.**
AskSin++ ist um globale Objekte gebaut (`hal`, `sdev`, `radio`). Auf dem Flipper
läuft das im ELF-Loader-Kontext mit unklarer Reihenfolge relativ zum Furi-Runtime-Start.
**Muster:** globale Objekte werden als rohe, ausgerichtete Puffer deklariert und im
Entry-Point per *placement new* konstruiert, in explizit definierter Reihenfolge:

```cpp
alignas(HmDevice) static uint8_t g_dev_storage[sizeof(HmDevice)];
static HmDevice* g_dev = nullptr;
// im Entry-Point, nach furi_record_open():
g_dev = new (g_dev_storage) HmDevice(g_hal, ...);
```
Destruktoren werden am Ende symmetrisch und **explizit** aufgerufen (`g_dev->~HmDevice()`).

**Regel 3.2 — Kein Heap im Funkpfad.** `malloc`/`new` sind im Radio-Thread ab
`hal.runready()` tabu. Alle Puffer sind statisch dimensioniert. Allokation ist
ausschließlich in der Init-/Deinit-Phase erlaubt (dort dann `malloc`/`free` aus
`furi/core/memmgr.h`, das bei OOM sauber crasht — kein `nullptr`-Check-Theater).

**Regel 3.3 — Keine C++-Standardbibliothek mit dynamischem Verhalten.**
Erlaubt: `<stdint.h>`, `<string.h>`, `<new>` (placement), `<type_traits>`, `<utility>`.
Verboten: alles, was allokiert oder Exceptions braucht.

**Regel 3.4 — C-Linkage an der Grenze.** Jeder Header aus `port/`, der von
`.c`-Dateien oder von Furi-Callbacks genutzt wird, kapselt sich in
`#ifdef __cplusplus extern "C" { #endif`. Furi-Callbacks (Draw, Input, GPIO-EXTI,
Timer) sind freie `static` C-Linkage-Funktionen, die auf `context` casten — **nie**
nicht-statische Memberfunktionen.

---

## 4. HAL-Bridging: Arduino/AVR → Furi

Alle Shims leben in `lib/asksinpp/port/arduino_compat/`. Ziel: AskSin++ soll die
Shims includieren können, ohne dass in `upstream/` mehr als der Include-Pfad
verbogen wird.

### 4.1 Mapping-Tabelle (verbindlich)

| Arduino/AVR | Flipper/Furi | Anmerkung |
|---|---|---|
| `millis()` | `furi_get_tick()` | FreeRTOS-Tickrate ist 1 kHz → 1 Tick == 1 ms. Trotzdem nie hartcodieren: `furi_ms_to_ticks()` benutzen, wo eine Umrechnung nötig ist. |
| `micros()` | `DWT`-Zähler via `furi_hal_cortex_timer_get()` | Nur wo AskSin++ es wirklich braucht. Kein Free-Running-µs-Counter im API — nicht erfinden. |
| `delay(ms)` | `furi_delay_ms(ms)` | Blockierend, gibt CPU ab. Im Radio-Thread nur außerhalb kritischer Timings. |
| `delayMicroseconds(us)` | `furi_delay_us(us)` | Busy-Wait. Max. ~1000 µs am Stück, sonst Scheduler-Hunger. |
| `_delay_ms()` / `_delay_us()` | wie oben | AVR-Makros aus `util/delay.h` werden im Shim ersetzt. |
| `cli()` / `sei()` | `FURI_CRITICAL_ENTER()` / `FURI_CRITICAL_EXIT()` | **Nur um reine Speicherzugriffe.** Nie über SPI, Storage, Logging oder Delays. Ziel: < 10 µs. |
| `PROGMEM`, `PSTR` | leer definieren | ARM ist von-Neumann; Flash ist normal adressierbar. |
| `pgm_read_byte/word/ptr`, `memcpy_P`, `strcmp_P` | direkter Zugriff / `memcpy` / `strcmp` | |
| `attachInterrupt(GDO0)` | `furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeInterruptRise, GpioPullNo, GpioSpeedLow)` + `furi_hal_gpio_add_int_callback()` + `furi_hal_gpio_enable_int_callback()` | Beide Symbole sind `+`. Siehe §5.4 — Interrupt ist **optional**, Polling ist der Default. |
| `Serial` / `DPRINT` / `DHEX` / `DDEC` | eigener `Debug.h`-Shim → `FURI_LOG_D/I/W/E` | Siehe §8. |
| `EEPROM` / `InternalEprom` | `FlipperEeprom` (RAM-Shadow + Datei) | Siehe §6. |
| `LowPower` / `sleep_mode()` | No-Op-Klasse | Der Flipper schläft nicht während die App läuft. Zusätzlich `furi_hal_power_insomnia_enter()` im App-Start. |
| `wdt_reset()` / Watchdog | No-Op | |
| `analogRead(Battery)` | `furi_hal_power_get_battery_voltage(FuriHalPowerICFuelGauge)` | Auf die von AskSin++ erwartete Skala (Zehntel-Volt / `uint8_t`) umrechnen. |
| LED / `pinMode(LED)` | `furi_hal_light_set(LightRed/Green/Blue, v)` oder Notification-Record | Für UI-Feedback bevorzugt der Notification-Record. |

**Regel 4.1 — ISR-Kontext ist heilig.** Der GDO0-EXTI-Callback läuft im
Interrupt-Kontext. Erlaubt sind dort ausschließlich: ein `volatile`-Flag setzen
oder `furi_thread_flags_set()`. **Verboten:** SPI, Logging, Mutex-Take, `malloc`,
jede AskSin++-Funktion.

**Regel 4.2 — Tick-Overflow.** `furi_get_tick()` ist `uint32_t` und läuft nach
~49,7 Tagen über. Zeitvergleiche immer als Differenz (`(uint32_t)(now - then) >= timeout`),
nie als `now >= deadline`.

### 4.2 Clock / Alarm

AskSin++ braucht eine getaktete `AlarmClock` (`as::sysclock`). Der AVR-Timer1-Pfad
in `upstream/AlarmClock.h` ist hinter `ARDUINO_ARCH_AVR` — da wir keinen
`ARDUINO_ARCH_*` setzen, übersetzen `SysClock::init()/enable()/disable()` zu
leeren Rümpfen. Die Uhr steht also still, bis jemand sie taktet.

**Regel 4.3 — `TICKS_PER_SECOND` bleibt bei 100 (Upstream-Default, 10 ms/Tick).**
Nicht auf 1000 erhöhen. `Alarm::tick` ist ein **24-Bit-Bitfeld**
(`upstream/Alarm.h:31`). Bei 1000 Ticks/s wäre der größte darstellbare Alarm
~4,6 h, und `seconds2ticks(65535)` — wie es Kanaltimer und BatterySensor
verwenden — läuft still über. Sämtliche Timing-Konstanten der Bibliothek sind
auf 100/s ausgelegt; BidCos braucht nicht feiner (ACK-Fenster ~200 ms).

**Regel 4.4 — Getaktet wird aus dem Radio-Thread, nicht per Timer-ISR.**
`port/flipper_sysclock.h` zieht bei jedem Schleifendurchlauf die verstrichenen
Ticks nach (`--as::sysclock`). Dadurch läuft die komplette Alarmverarbeitung im
Thread-Kontext und braucht keinerlei Synchronisation gegen AskSin++-Zustand.
Differenzbildung, kein Vergleich — der Tickzähler läuft über.

**Regel 4.5 — `TICKS_PER_SECOND`, `E2END`, `NORTC`, `ARDUINO`,
`ASKSIN_FLIPPER_PORT` und `STORAGEDRIVER` stehen in `application.fam`, nicht nur
in `flipper_config.h`.** Die upstream-`.cpp`-Dateien sehen den Config-Header nie.
Wären die Werte nur dort gesetzt, hätten `AlarmClock.cpp` und der Rest der App
verschiedene Tickraten bzw. Storage-Größen — ein stiller ODR-Bruch, den kein
Compiler meldet.

---

## 5. Radio-Layer: AskSin++ ↔ CC1101

### 5.1 Grundlage (gegen SDK 1.4.3 verifiziert)

Der interne CC1101 ist im FAP-API **im Packet-Mode voll nutzbar** — es ist *kein*
direkter SPI-Zugriff nötig. Verfügbar (alle `+`):

```
subghz_devices_init / _get_by_name / _begin / _end / _reset / _idle / _sleep
subghz_devices_load_preset / _set_frequency / _is_frequency_valid
subghz_devices_set_tx / _set_rx / _flush_tx / _flush_rx
subghz_devices_write_packet / _read_packet / _rx_pipe_not_empty
subghz_devices_is_rx_data_crc_valid / _get_rssi / _get_lqi
```

**Regel 5.1 — `subghz_devices_*` ist die einzige erlaubte Radio-API.**
Nicht `furi_hal_subghz_*` direkt (dort ist `init` nicht exportiert und die Device-
Abstraktion würde umgangen), nicht `furi_hal_spi_*`, nicht die Async-/Raw-Modi
(`start_async_tx/rx` sind für OOK-Replay gedacht und für BidCos ungeeignet).
Nebeneffekt: ein späterer Umstieg auf ein externes CC1101-Modul ist ein
Einzeiler beim Device-Namen.

### 5.2 Initialisierungssequenz (fixe Reihenfolge)

```cpp
subghz_devices_init();
const SubGhzDevice* dev = subghz_devices_get_by_name("cc1101_int");
subghz_devices_begin(dev);
subghz_devices_reset(dev);
subghz_devices_load_preset(dev, FuriHalSubGhzPresetCustom, asksin_preset);
subghz_devices_set_frequency(dev, 868300000);   // NACH dem Preset — überschreibt FREQ2/1/0
subghz_devices_flush_rx(dev);
subghz_devices_set_rx(dev);
```

Teardown spiegelbildlich: `idle` → `sleep` → `end` → `deinit`.

### 5.3 Register-Preset

**Regel 5.3.1 — Registerwerte werden 1:1 aus AskSin++ `cc1101.h` (`initVal[]`)
übernommen. Sie werden nicht geraten, nicht "optimiert" und nicht aus einem
Flipper-Preset abgeleitet.** BidCos verlangt eine exakte Konfiguration
(2-FSK, ~10 kBaud, Syncword `0xE9CA`, variable Paketlänge, Whitening, CRC,
`PKTLEN` passend zur BidCos-Maximallänge). Ein einziges falsches Register
bedeutet: die CCU sieht das Gerät nie, ohne jede Fehlermeldung.

**Regel 5.3.2 — Preset-Format.** `FuriHalSubGhzPresetCustom` erwartet ein Array aus
`(reg, value)`-Paaren, terminiert durch `0x00, 0x00`, gefolgt von genau 8 PATABLE-Bytes.
Dieses Layout ist aus dem Firmware-Verhalten abgeleitet und beim ersten Hardware-Test
zu bestätigen (`// TODO(verify)` bis dahin). Die Adressen sind CC1101-Registeradressen,
**keine** Flipper-Abstraktion.

**Regel 5.3.3 — GDO0-Konfiguration (`IOCFG0`) ist Teil des Presets** und muss so
gesetzt sein, wie AskSin++ es erwartet (Assert bei Sync-Word, Deassert am Paketende).
Sie wird nicht separat gepatcht.

### 5.4 RX-Strategie

**Default: Polling im Radio-Thread**, nicht GDO0-Interrupt. Begründung: BidCos ist
mit ~10 kBaud langsam, `subghz_devices_rx_pipe_not_empty()` bei ~2–5 ms Loop-Periode
ist mehr als schnell genug, und es vermeidet ISR-Komplexität sowie einen Konflikt
mit `gpio_cc1101_g0`, das die Firmware in Async-Modi selbst belegt.
Der Interrupt-Pfad bleibt als Option in §4.1 dokumentiert, wird aber **erst
implementiert, wenn Polling messbar zu spät kommt.**

Empfangspfad: `rx_pipe_not_empty()` → `is_rx_data_crc_valid()` → `read_packet()`
→ an AskSin++ `Radio::read()` weiterreichen. Bei ungültiger CRC: `flush_rx()` und
verwerfen, ohne die AskSin++-Statemachine anzufassen.

### 5.5 TX-Strategie

* `set_tx()` gibt `bool` zurück und ist **False**, wenn die Region-Prüfung der
  Firmware die Frequenz sperrt. Dieser Rückgabewert **muss** ausgewertet und im UI
  als klarer Fehler angezeigt werden ("TX blocked by region"). Nicht ignorieren.
* Nach TX unverzüglich zurück nach RX — BidCos-ACKs kommen im ~200-ms-Fenster.
* **Burst-Modus**: BidCos sendet für Wake-on-Radio-Empfänger eine lange Präambel
  (Größenordnung 360 ms Carrier) vor dem eigentlichen Paket. Für eine Fernbedienung
  Richtung CCU ist Burst normalerweise **nicht** nötig; falls doch, ist das ein
  eigener, explizit dokumentierter Pfad und kein Default.
* TX-Timing wird **nie** durch `furi_delay_ms()` innerhalb einer Critical Section
  oder mit gehaltenem Mutex erzeugt.

**Regel 5.5.1 — Der Mindest-Sendeabstand läuft über `furi_get_tick()`, niemals
über einen `sysclock`-Alarm.** `upstream/Radio.h::MinSendTimeout::waitTimeout()`
busy-wartet darauf, dass ein `sysclock`-Alarm feuert. Bei uns wird `sysclock`
aber vom Radio-Thread getaktet (§4.4) — genau dem Thread, der dort warten würde.
Das ist ein garantierter Deadlock. `port/flipper_radio.cpp` rechnet den nächsten
zulässigen Sendezeitpunkt deshalb direkt in Furi-Ticks aus. Wer hier auf die
Upstream-Mechanik zurückbaut, hängt die App auf.

**Regel 5.5.2 — Burst wird nicht vorgetäuscht.** Ein echter BidCos-Burst ist ein
~360 ms Dauerträger vor dem Paket; über die Packet-Mode-API ist er nicht
erzeugbar. `write()` loggt eine Warnung und sendet ohne Burst. Für den Pfad
Fernbedienung → Zentrale ist das korrekt. Nicht durch wiederholtes Senden des
Pakets „nachbauen" — das ist kein Burst und stört nur den Kanal.

### 5.6 Frequenz & Region

868,3 MHz liegt im SRD-Band und in der erlaubten 868-MHz-Bandgruppe der Firmware
(Pfad `FuriHalSubGhzPath868` wird von `set_frequency` mitgesetzt).
Vor `set_tx()` einmalig `subghz_devices_is_frequency_valid()` prüfen und das
Ergebnis loggen.

---

## 6. Storage: EEPROM-Ersatz

AskSin++ persistiert Device-ID, HMID der Zentrale, Peers und RegLists über eine
`InternalEprom`-artige Schnittstelle (`getByte`/`setByte`/`getData`/`setData`/`clearData`).

**Regel 6.1 — RAM-Shadow, nicht Durchschreiben.** Implementierung als
`FlipperEeprom` mit einem statischen `uint8_t`-Array (Größe fix, z. B. 1024 B,
über `#define` in `port/config.h` zentral definiert). Alle AskSin++-Zugriffe gehen
nur gegen dieses Array. **Niemals** pro Byte auf die SD-Karte schreiben —
AskSin++ schreibt byteweise, das würde die Karte und das Timing zerlegen.

**Regel 6.2 — Explizites, verzögertes Flush.**
* `setByte`/`setData` setzen nur ein `dirty`-Flag.
* Geflusht wird: (a) debounced ~1 s nach der letzten Änderung, (b) beim App-Exit,
  (c) nach erfolgreichem Pairing.
* Das Flush läuft **nicht** im Radio-Thread während eines offenen TX/ACK-Fensters.
  Entweder im App-Thread oder im Radio-Thread an einem definierten, funkfreien Punkt.

**Regel 6.3 — Dateiformat & Ort.**
* Pfad: `APP_DATA_PATH("eeprom.bin")` → wird zu `/data/eeprom.bin` und vom
  Storage-Service auf `/ext/apps_data/<appid>/` gemappt.
* Verzeichnis vorher mit `storage_common_mkdir()` anlegen; `FSE_EXIST` ist kein Fehler.
* Header vor den Nutzdaten: Magic (`"HMRC"`), Version (`uint8_t`), Payload-Größe
  (`uint16_t`), CRC über die Nutzdaten. Bei Mismatch: Datei verwerfen, Shadow mit
  `0xFF` initialisieren (AVR-EEPROM-Semantik!) — AskSin++ erkennt daran den
  Auslieferungszustand und legt seine Defaults an.
* Schreiben immer **atomar**: nach `eeprom.bin.tmp` schreiben, dann umbenennen.

**Regel 6.4 — Device-ID/Serial sind persistent, nicht zufällig pro Start.**
Beim allerersten Start werden HMID und Seriennummer einmalig erzeugt und
gespeichert. Ein Reflash der App darf ein angelerntes Gerät nicht "vergessen"
und darf keine Duplikate im Homematic-Netz erzeugen.

---

## 7. GUI, Threading und Event-Loop

### 7.1 Thread-Aufteilung (verbindlich)

Drei Kontexte, klar getrennt:

1. **GUI-Service-Thread (fremd):** ruft nur den `ViewPort`-Draw-Callback auf.
   Darf **ausschließlich** aus dem Modell-Snapshot lesen, nichts berechnen,
   nichts funken, nichts speichern.
2. **App-Thread (Entry-Point):** besitzt Records (`gui`, `notification`, `storage`),
   ViewPort und Input-Queue. Konsumiert Input-Events, übersetzt sie in Kommandos
   und schickt sie an den Radio-Thread. Blockiert in `furi_message_queue_get()`.
3. **Radio-Thread (eigener `FuriThread`, Priorität > Normal):** besitzt die gesamte
   AskSin++-Welt exklusiv. Läuft als enger Loop:
   `Kommando-Queue mit Timeout ~2 ms pollen → Kommando auf AskSin++ anwenden →
   RX-Poll → sysclock nachziehen → hal.runready() → Statusstruktur aktualisieren`.

**Regel 7.1 — Kein AskSin++-Objekt wird jemals außerhalb des Radio-Threads
berührt.** Weder lesend noch schreibend, auch nicht "nur kurz für die Anzeige".

### 7.2 Kommunikation

* **App → Radio:** `FuriMessageQueue` mit einem POD-Kommando-Struct
  (`{ CmdType type; uint8_t channel; bool longpress; }`). Nicht-blockierend senden;
  volle Queue wird geloggt und verworfen (ein verlorener Tastendruck ist besser
  als ein blockierter UI-Thread).
* **Radio → GUI:** eine einzige `RadioStatus`-POD-Struktur, geschützt durch
  `FuriMutex`. Der Radio-Thread schreibt sie, der Draw-Callback kopiert sie unter
  Mutex in eine lokale Kopie und rendert dann **ohne** gehaltenen Mutex.
  Mutex-Haltedauer: nur `memcpy`, nie Canvas-Aufrufe.
* Nach Statusänderung: `view_port_update()` aus dem Radio-Thread. Das ist
  thread-safe und der einzige erlaubte GUI-Aufruf von dort.

### 7.3 UI-Verhalten

* Der Draw-Callback ist rein: gleicher Snapshot → gleiches Bild, keine Seiteneffekte.
* Angezeigt werden mindestens: aktueller Kanal 1–8, Pairing-Status
  (unpaired / pairing / paired mit HMID), letzte TX-Quittierung (ACK/NACK/Timeout),
  RSSI des letzten Empfangs.
* Steuerung: Steuerkreuz wählt den Kanal, OK sendet. Kurzer vs. langer Druck des
  OK-Buttons wird über `InputTypeShort` / `InputTypeLong` auf AskSin++
  short-/long-press gemappt.
* `InputTypeRepeat` bei gehaltenem OK → AskSin++ erwartet für Longpress
  **wiederholte** Telegramme mit inkrementierendem Zähler, nicht ein einzelnes.
  Dieses Detail ist der häufigste Grund, warum Longpress an der CCU nicht ankommt.
* Back-Taste: kurz = zurück/Abbruch, lang = App beenden (mit Flush, siehe §6.2).

### 7.4 Shutdown-Reihenfolge

`Radio-Thread stoppen (Flag + join)` → `EEPROM-Flush` → `Radio-Teardown (§5.2)` →
`ViewPort abmelden & freigeben` → `Queues/Mutex freigeben` → `Records schließen` →
`furi_hal_power_insomnia_exit()`. Jeder `furi_record_open()` braucht genau ein
`furi_record_close()` — sonst hängt der Flipper nach App-Ende.

---

## 8. Logging & Debugging

* Ausschließlich `FURI_LOG_D/I/W/E(TAG, ...)` mit `#define TAG "HmRc8"`. Kein `printf`.
* Der AskSin++-`Debug.h`-Shim mappt `DPRINT`/`DHEX`/`DDEC`/`DDECLN` auf `FURI_LOG_D`.
  Da AskSin++ zeichenweise/stückweise ausgibt, puffert der Shim zeilenweise und
  loggt erst beim Newline — sonst ist die Ausgabe unlesbar.
* **Regel 8.1 — Kein Logging im Funk-Zeitfenster.** Zwischen TX und ACK-Empfang
  wird nicht geloggt; Ereignisse werden in einen Ringpuffer geschrieben und danach
  ausgegeben. UART-Logging verzögert im Millisekundenbereich und zerstört
  BidCos-Timings.
* Telegramm-Dumps (hex) nur hinter einem `#define HMRC8_TRACE_TELEGRAMS`, per
  Default aus.
* Log lesen: `ufbt cli` → `log`.

---

## 9. AskSin++-Gerätedefinition (HM-RC-8)

**Regel 9.1 — `DeviceInfo` wird nicht erfunden.** Die Struktur ist:
Device-ID (3 B), Serial (10 ASCII), **Device-Model (2 B)**, Firmware-Version (1 B),
DeviceType, Info-Bytes (2 B).
Model-ID, Firmware-Version und Info-Bytes **müssen exakt zu einem Eintrag in den
`rftypes`-XMLs der Zielzentrale passen** (RaspberryMatic: `/opt/HomeMatic/etc/config/rftypes/`).
Passt die Kombination nicht, akzeptiert die CCU das Anlernen entweder gar nicht
oder legt ein Gerät mit falscher Kanalanzahl an.
→ Diese Werte werden **aus der Zentrale ausgelesen und im Code mit Quellenangabe
kommentiert**, nicht aus dem Gedächtnis eingesetzt. Bis dahin stehen sie als
`// TODO(verify): aus rftypes/rf_rc_8.xml` im Code.

**Regel 9.2 — Serial ist eindeutig.** 10 Zeichen, muss über alle Geräte im
Homematic-Netz eindeutig sein. Wird bei Erstinitialisierung erzeugt und
persistiert (§6.4).

**Regel 9.3 — Kanalstruktur.** `MultiChannelDevice` mit 8 `RemoteChannel`-Instanzen,
`List0` für Geräteregister, `List4` für Peer-Register. Peer-Anzahl pro Kanal
konservativ (Default 4–6) — jeder Peer-Slot kostet EEPROM-Shadow (§6.1).

**Regel 9.4 — Pairing-Modus.** Config-Taste des Originalgeräts wird als
UI-Aktion nachgebildet (z. B. langer Druck auf eine dedizierte Taste). Der
Anlernvorgang läuft ausschließlich über die reguläre AskSin++-Pairing-Logik
(`sdev.startPairing()`); es wird **keine** HMID der Zentrale hardcodiert und
kein Pairing-Zustand manuell in den EEPROM-Shadow geschrieben.

**Regel 9.5 — AES/Signing.** Für Meilenstein 1 deaktiviert. Falls die Zentrale
Signing erzwingt, ist das ein eigener Meilenstein — nicht nebenbei einbauen.

---

## 10. Definition of Done

Ein Schritt gilt erst als fertig, wenn:

1. `ufbt` fehlerfrei durchläuft — **Warnungen im eigenen Code (`src/`, `port/`)
   sind Fehler.** Warnungen aus `upstream/` werden per `Lib(cflags=...)` gedämpft,
   nicht per globalem `-w`.
2. `ufbt launch` die App auf der Hardware startet und sie sich sauber beenden
   lässt (kein Furi-Crash, kein hängender Record, keine Speicherlecks laut
   `free`-Kommando in der CLI vor/nach dem Lauf).
3. Für jede angefasste Firmware-API der `+`-Status aus `api_symbols.csv` belegt ist.
4. Jedes `// TODO(verify)` entweder aufgelöst oder im Antworttext explizit als
   offener Punkt benannt ist.
5. Bei Funkänderungen: gegen die echte CCU getestet, mit Angabe was beobachtet wurde.
   **"Sollte funktionieren" ist kein Testergebnis** — nicht getestete Änderungen
   werden als ungetestet gekennzeichnet.

---

## 11. Anti-Patterns (nicht tun)

* Firmware-Funktionen aufrufen, die in `api_symbols.csv` mit `-` stehen.
* `furi_hal_subghz_init()` aufrufen (siehe §1.2).
* Einen AskSin++- oder Arduino-Header unter `src/` inkludieren (§2.2.2).
* `TICKS_PER_SECOND` erhöhen (§4.3).
* Den Sendeabstand auf einen `sysclock`-Alarm umbauen (§5.5.1).
* AskSin++-Zustand aus dem GUI- oder App-Thread lesen.
* CC1101-Register „nach Gefühl" setzen oder aus einem Flipper-OOK-Preset ableiten.
* Pro Byte auf die SD-Karte schreiben.
* `std::vector`/`std::string`/`std::function` oder irgendetwas Exception-basiertes.
* Globale C++-Objekte mit nicht-trivialen Konstruktoren.
* Async-/Raw-SubGhz-Modi für BidCos verwenden.
* Blockierende Operationen (Storage, Log, `delay`) im ACK-Zeitfenster.
* Retry-/Timeout-Werte von AskSin++ „anpassen", weil etwas nicht geht — die Werte
  sind protokollbestimmt. Erst die Ursache suchen.
* `upstream/` direkt editieren statt Patch oder Shim.

---

## 12. Recht & Betrieb

* 868,3 MHz ist ein SRD-Band mit Duty-Cycle-Beschränkung (in DE typ. 1 % für
  868,0–868,6 MHz). Die App darf keine Dauersende- oder Flood-Funktion bekommen;
  Sendevorgänge bleiben an bewusste Benutzeraktionen gekoppelt.
* Die App emuliert ein Gerät **im eigenen** Homematic-Netz. Es werden keine
  Funktionen implementiert, die auf fremde Installationen zielen (Scannen fremder
  HMIDs, Rolling-Code-Angriffe, Impersonation angelernter Fremdgeräte).
* Die Region-Sperre der Firmware wird respektiert, nicht umgangen (§5.5).
* Homematic ist eine Marke von eQ-3; dieses Projekt ist inoffiziell und
  unabhängig. AskSin++ steht unter der Lizenz des Upstream-Projekts —
  `LICENSE` in `lib/asksinpp/upstream/` bleibt erhalten.

---

## 12a. Stand der Umsetzung

**Verifiziert (gebaut, `ufbt` grün, Target 7 / API 87.1, FAP 14,7 KB):**

| Baustein | Datei | Status |
|---|---|---|
| Arduino-/AVR-Shim | `port/arduino_compat/` | fertig |
| Debug → `FURI_LOG` | `arduino_compat/Arduino.h` | fertig, zeilengepuffert |
| Systemuhr | `port/flipper_sysclock.h` | fertig |
| EEPROM-Ersatz | `port/flipper_eeprom.*` | fertig, atomar + CRC |
| Radio (CC1101) | `port/flipper_radio.*` | fertig, **nicht auf HW getestet** |
| C++-Runtime | `port/cxx_runtime.cpp` | fertig |
| Fassade | `port/hm_service.*` | Plumbing fertig, Gerätelogik offen |
| HM-RC-8-Gerät | — | **offen**, blockiert (siehe unten) |
| GUI | — | **offen** |

**Angewandte Patches:** `patches/001-radio-flipper-port.patch` — schneidet in
`upstream/Radio.h` den kompletten AVR-Funkstack heraus (eigene SPI-Klassen,
GDO0-ISR, Treiber für CC1101/Si4431/RFM69). Nötig, weil `Radio-Si4431.h:172` ein
`WRITE_REG` definiert, das mit CMSIS (`stm32wbxx.h`) kollidiert und dadurch jeden
STM32-LL-Header zerlegt. Ein Shim reicht hier nicht: `Device.h:15` inkludiert
`"Radio.h"` in Anführungszeichen, quoted-Includes werden zuerst relativ zum
inkludierenden File aufgelöst — `upstream/Radio.h` gewinnt also immer.
Genau eine Upstream-Datei ist verändert.

**Offene `TODO(verify)` im Code:**
* Preset-Layout von `FuriHalSubGhzPresetCustom` (Paare + `0,0` + 8 PATABLE-Bytes)
* Umrechnung Flipper-RSSI (dBm) → CC1101-Rohwert
* GDO0-Polarität beim Warten auf Sendeende
* PATABLE-Wert `0x03` aus Upstream ist sehr niedrige Sendeleistung — erster
  Stellknopf, falls die Reichweite zur Zentrale nicht genügt, aber **erst nach
  einem Test**

**Blockierend für den nächsten Schritt:** Device-Model, Firmware-Version und
Info-Bytes des HM-RC-8 müssen aus den `rftypes`-XMLs der Zielzentrale kommen
(RaspberryMatic: `/opt/HomeMatic/etc/config/rftypes/`). Ohne diese Werte lässt
sich §9.1 nicht einhalten. Ebenso offen: einmalig erzeugte, persistierte HMID
und Seriennummer (§6.4) — die aktuell fest verdrahtete `0x123456` darf so nicht
in Betrieb gehen.

---

## 13. Arbeitsweise in diesem Projekt

* Vor einer Änderung: relevante Header aus `~/.ufbt/current/sdk_headers/` lesen.
* Änderungen klein halten und pro Schicht (Shim / Radio / Storage / Device / UI)
  getrennt liefern, damit sie einzeln auf Hardware verifizierbar sind.
* Bei Konflikt zwischen „schnell zum Laufen bringen" und „Protokoll korrekt":
  Protokoll gewinnt. Ein Gerät, das sich anlernt aber sporadisch ACKs verliert,
  ist schlimmer als eines, das gar nicht anlernt.
* Unsicherheit wird benannt, nicht überspielt.
