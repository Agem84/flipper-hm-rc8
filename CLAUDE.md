# CLAUDE.md — Flipper Zero FAP: Homematic BidCos Remote (AskSin++)

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

**Verifizierte Umgebung (Stand 2026-09-21):**

| Komponente | Wert |
|---|---|
| Build-Tool | `ufbt` (Python-Wheel, in `PATH`) |
| **Build-SDK** | `~/.ufbt-unleashed/current` — **Unleashed `unlshd-086`**, Target `f7` |
| **API-Version** | **`87.6`** (`sdk_headers/f7_sdk/targets/f7/api_symbols.csv`) |
| Firmware auf dem Testgerät | Unleashed `unlshd-086`, API 87.6 — **identisch zum Build-SDK, und das muss so bleiben** |
| Toolchain | `arm-none-eabi-gcc` aus dem ufbt-Toolchain-Cache |
| Altes SDK | `~/.ufbt/current`, offiziell `1.4.3` / API `87.1` — bleibt liegen, wird **nicht** mehr zum Bauen benutzt |

**Gebaut wird deshalb immer mit gesetztem `UFBT_HOME`:**

```bash
UFBT_HOME=~/.ufbt-unleashed ufbt
```

Unter Windows/PowerShell entsprechend `$env:UFBT_HOME = "$HOME\.ufbt-unleashed"` vor
dem `ufbt`-Aufruf. Wer das vergisst, baut gegen API 87.1 — der Build ist dann
grün und die App läuft auf dem Gerät trotzdem falsch (Begründung in §12a,
Abschnitt „Gerätefirmware weicht vom SDK ab").

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
hm_rc8/
├── application.fam            # FAP-Manifest
├── CLAUDE.md                  # diese Datei
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
| `analogRead()` | No-Op, liefert 0 | Der Shim fasst **keine** Hardware an. Die HAL nutzt `NoBattery`, es fragt also niemand. Erst wenn ein echter Batteriestatus an die Zentrale gemeldet werden soll, kommt ein eigener Adapter auf `furi_hal_power_get_battery_voltage(FuriHalPowerICFuelGauge)` — dann mit Umrechnung auf die von AskSin++ erwartete Skala (Zehntel-Volt / `uint8_t`). |
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
furi_hal_subghz_reset / _idle / _sleep / _rx / _tx
furi_hal_subghz_load_custom_preset / _load_registers / _load_patable
furi_hal_subghz_set_frequency_and_path / _is_frequency_valid
furi_hal_subghz_flush_tx / _flush_rx / _get_data_gpio
furi_hal_subghz_write_packet / _read_packet / _rx_pipe_not_empty
furi_hal_subghz_is_rx_data_crc_valid / _get_rssi / _get_lqi
```

**Regel 5.1 — `furi_hal_subghz_*` im Packet-Mode ist die einzige erlaubte
Radio-API.** Nicht `furi_hal_spi_*`, nicht die Async-/Raw-Modi
(`start_async_tx/rx` sind für OOK-Replay gedacht und für BidCos ungeeignet).

*Korrektur zur ursprünglichen Prämisse:* Hier stand früher
`subghz_devices_*` (die Device-Registry). Auf der Zielhardware schlug
`subghz_devices_begin()` fehl, während das Log gleichzeitig
„Library for subghz_radio_device, API v. 2" meldete — der ufbt-SDK-Header
definiert aber `SUBGHZ_RADIO_DEVICE_PLUGIN_API_VERSION 1`. Die
Registry-Semantik weicht auf dieser Firmware ab. Der Packet-Mode ist
vollständig auch ohne Registry erreichbar; `furi_hal_subghz_init()` ('-')
wird nicht gebraucht, die Firmware ruft sie beim Boot selbst auf.
Preis der Entscheidung: kein externes CC1101-Modul — laut §0 ohnehin
außerhalb des Scope.

### 5.2 Initialisierungssequenz (fixe Reihenfolge)

```cpp
// load_custom_preset ruft furi_hal_subghz_reset() selbst als erstes auf
// (firmware.elf @0x0800f744) — kein separater reset() davor nötig.
furi_hal_subghz_load_custom_preset(asksin_preset);
furi_hal_subghz_is_frequency_valid(868300000);
furi_hal_subghz_set_frequency_and_path(868300000); // NACH dem Preset — überschreibt FREQ2/1/0
furi_hal_gpio_init(gdo0, GpioModeInput, GpioPullNo, GpioSpeedLow); // siehe Regel 5.2.1
furi_hal_subghz_flush_rx();
furi_hal_subghz_rx();
```

Teardown spiegelbildlich: `idle` → `sleep` → GDO0 zurück auf `GpioModeAnalog`.

**Regel 5.2.1 — Nach *jedem* `furi_hal_subghz_reset()` muss GDO0 neu als
Eingang konfiguriert werden.** Die Firmware legt den Pin dort auf
`GpioModeAnalog` (`firmware.elf @0x0800f706`, `mode == 5`), und im
Analog-Modus ist der Schmitt-Trigger abgeschaltet — `furi_hal_gpio_read()`
liefert dann konstant 0. Das steckt auch in
`furi_hal_subghz_load_custom_preset()` drin, betrifft also **auch
`FlipperRadio::reset()`**, nicht nur `init()`. Wird es dort vergessen, ist
die Sync-Erkennung in `waitTxComplete()` dauerhaft tot und jeder
Sendevorgang fällt auf die rechnerische Dauer zurück.

### 5.3 Register-Preset

**Regel 5.3.1 — Registerwerte werden 1:1 aus AskSin++ `cc1101.h` (`initVal[]`)
übernommen. Sie werden nicht geraten, nicht "optimiert" und nicht aus einem
Flipper-Preset abgeleitet.** BidCos verlangt eine exakte Konfiguration
(2-FSK, ~10 kBaud, Syncword `0xE9CA`, variable Paketlänge, Whitening, CRC,
`PKTLEN` passend zur BidCos-Maximallänge). Ein einziges falsches Register
bedeutet: die CCU sieht das Gerät nie, ohne jede Fehlermeldung.

**Regel 5.3.2 — Preset-Format.** `furi_hal_subghz_load_custom_preset()` erwartet
ein Array aus `(reg, value)`-Paaren, terminiert durch `0x00, 0x00`, gefolgt von
genau 8 PATABLE-Bytes. Die Adressen sind CC1101-Registeradressen, **keine**
Flipper-Abstraktion. **Verifiziert** gegen `firmware.elf` 1.4.3,
`furi_hal_subghz_load_custom_preset @0x0800f734`.

**Regel 5.3.2a — Register `0x00` (IOCFG2) darf in der Tabelle NICHT vorkommen.**
Die Registerschleife der Firmware bricht beim ersten Registerbyte `0x00` ab:

```
800f75a: ldrb r4, [r5, r6]   ; r4 = preset[i]
800f75c: adds r6, #2
800f75e: cbnz r4, ...        ; r4 == 0  ->  Schleife ENDE
```

Steht IOCFG2 an Position 0, wird **kein einziges Register geschrieben**, und die
8 PATABLE-Bytes werden aus den Folgebytes der Tabelle gelesen. Der CC1101 bleibt
dann auf Reset-Defaults (115 kBaud, Syncword `D391`, kein Whitening) — die
Zentrale kann uns unmöglich hören, und es gibt keinerlei Fehlermeldung.
Genau dieser Fehler hat den ersten Hardware-Test scheitern lassen.

Die firmwareeigenen Presets lassen `0x00` aus demselben Grund weg
(`subghz_device_cc1101_preset_gfsk_9_99kb_async_regs @0x080a3f8e` beginnt mit
`02 06`). Ein einzelnes Register ist aus einem FAP nicht setzbar:
`furi_hal_subghz_write_reg` ist nicht exportiert, und
`furi_hal_subghz_load_registers` hat dieselbe Terminator-Semantik.
IOCFG2 bleibt daher auf dem Wert aus `furi_hal_subghz_reset()` (`0x29`,
CHIP_RDY) statt auf Upstreams `0x2E` — folgenlos, GDO2 ist unbeschaltet.

**Regel 5.3.3 — GDO0-Konfiguration (`IOCFG0`) ist Teil des Presets** und muss so
gesetzt sein, wie AskSin++ es erwartet (Assert bei Sync-Word, Deassert am Paketende).
Sie wird nicht separat gepatcht. Der zugehörige **Pin**-Modus dagegen schon —
siehe Regel 5.2.1.

### 5.4 RX-Strategie

**Default: Polling im Radio-Thread**, nicht GDO0-Interrupt. Begründung: BidCos ist
mit ~10 kBaud langsam, `subghz_devices_rx_pipe_not_empty()` bei ~2–5 ms Loop-Periode
ist mehr als schnell genug, und es vermeidet ISR-Komplexität sowie einen Konflikt
mit `gpio_cc1101_g0`, das die Firmware in Async-Modi selbst belegt.
Der Interrupt-Pfad bleibt als Option in §4.1 dokumentiert, wird aber **erst
implementiert, wenn Polling messbar zu spät kommt.**

Empfangspfad: **GDO0 abwarten** → `rx_pipe_not_empty()` → `is_rx_data_crc_valid()`
→ **RSSI lesen** → `read_packet()` → `flush_rx()` → an AskSin++ `Radio::read()`
weiterreichen. Bei ungültiger CRC: `flush_rx()` und verwerfen, ohne die
AskSin++-Statemachine anzufassen.

**Regel 5.4.3 — Vor jedem Zugriff auf FIFO, CRC-Bit und RSSI wird gewartet,
bis GDO0 wieder low ist.** `furi_hal_subghz_rx_pipe_not_empty()` meldet nicht
„ein Telegramm ist da", sondern nur `NUM_RXBYTES > 0` — also bereits das erste
Byte eines **noch laufenden** Empfangs (`furi_hal_subghz.c`, liest
`CC1101_STATUS_RXBYTES`). Ein BidCos-Telegramm dauert bei ~10 kBaud 20–30 ms,
der Polltakt ist 10 ms: der Poll trifft also fast immer mitten hinein. Das
CRC_OK-Bit im LQI-Register steht dann noch nicht, `is_rx_data_crc_valid()`
liefert false — und ein `flush_rx()` an dieser Stelle **löscht das gerade
einlaufende Telegramm**. Damit gehen praktisch alle Antworten der Zentrale
verloren und das Anlernen kann nicht zustande kommen.
Die Firmware macht es in ihrem eigenen Packet-Mode-Worker genau so
(`lib/subghz/subghz_tx_rx_worker.c`, `subghz_tx_rx_worker_rx()`: erst
`while(furi_hal_gpio_read(g0))`, dann FIFO auswerten). `IOCFG0 = 0x06` aus dem
Preset liefert dafür die richtige Flanke. Wartezeit begrenzen (80 ms) und bei
Ablauf den FIFO zurücksetzen, sonst hängt ein Overflow den Empfänger auf.

**Regel 5.4.4 — Der Empfänger wird regelmäßig neu scharf gestellt.**
`MCSM1 = 0x03` bedeutet `RXOFF_MODE = IDLE`: nach **jedem** abgeschlossenen
Empfang fällt der CC1101 nach IDLE. Nach einem sauberen Telegramm fängt das
`flush_rx()` am Ende von `read()` ab. Der stille Fall ist ein Telegramm mit
CRC-Fehler: `PKTCTRL1 = 0x0C` hat CRC_AUTOFLUSH, der Chip räumt den FIFO selbst
ab und geht nach IDLE — für uns sieht das aus wie „nichts empfangen", und der
Empfänger bleibt **dauerhaft taub**, bis der nächste eigene Sendevorgang ihn
weckt. Upstream hat das Problem nicht, weil `rcvData()` jeden Aufruf mit
`flushrx()` + `SRX` beendet (`Radio-CC1101.h:551`) und nur aus der GDO0-ISR
gerufen wird. Beim Pollen wird deshalb entzerrt: alle 100 ms einmal
`idle → flush_rx → rx`, und zwar nur, wenn GDO0 low ist.
**Nicht** stattdessen `MCSM1` umdefinieren — Registerwerte kommen 1:1 aus
Upstream (§5.3.1).

**Regel 5.4.1 — `get_rssi()` wird VOR dem Flush gelesen.** Der Wert ist der
Live-Pegel des Empfängers; nach `idle()` + `flush_rx()` + erneutem `rx()` steht
dort der frisch gestartete Rauschpegel. AskSin++ trägt ihn über
`Device::sendAck()` in ausgehende Telegramme ein, ein falscher Wert geht also an
die Zentrale.

**Regel 5.4.2 — Der Empfangspuffer ist genau `ASKSIN_MAX_RAW_LEN` = 64 Byte
groß.** `cc1101_read_fifo` (`firmware.elf @0x0801d3c8`) klemmt die gelieferte
Länge hart auf 64 (`cmp #64` / `movcs #64`), unabhängig vom Längenbyte auf dem
Funk. Der Größenparameter von `furi_hal_subghz_read_packet()` ist reiner
Ausgabewert und wird nicht gelesen — ihn vorzubelegen ist wirkungslos.

### 5.5 TX-Strategie

* `furi_hal_subghz_tx()` gibt `bool` zurück und ist **False**, wenn die
  Region-Prüfung der Firmware die Frequenz sperrt. Dieser Rückgabewert **muss**
  ausgewertet und im UI als klarer Fehler angezeigt werden ("TX gesperrt
  (Region)"). Nicht ignorieren. Der Weg dorthin läuft über
  `FlipperRadio::txResult()` → `HmStatus::tx_result` (§7.3).
* Nach TX unverzüglich zurück nach RX — BidCos-ACKs kommen im ~200-ms-Fenster.
  `MCSM1 = 0x03` erledigt das bereits selbstständig.

**Regel 5.5.3 — Nach TX wird nicht blind `flush_rx()` aufgerufen.**
`waitTxComplete()` kehrt erst bis zu ~40 ms nach dem Paketende zurück (bei
totem GDO0 später). Ein früh eintreffendes ACK liegt zu dem Zeitpunkt bereits
im RX-FIFO und würde gelöscht. Aufgeräumt wird nur, wenn
`rx_pipe_not_empty()` false meldet.
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
* `setByte`/`setData` setzen nur ein `dirty`-Flag und ziehen den
  Entprellzeitpunkt nach (`FlipperEeprom::markDirty()`).
* Geflusht wird: (a) debounced `ASKSIN_EEPROM_FLUSH_DEBOUNCE_MS` (1 s) nach der
  letzten Änderung über `flushIfDue()`, (b) beim App-Exit, (c) beim Übergang nach
  „angelernt".
* Das Flush läuft **nicht** im Radio-Thread während eines offenen TX/ACK-Fensters.
  Konkreter Ort: **ganz am Anfang von `hm_service_poll()`**, vor `pump()`,
  `runready()` und `pollRadio()`. Das ist der einzige garantiert funkfreie Punkt
  der Schleife — `hm_service_button()` ist dort bereits vollständig abgearbeitet,
  weil `Device::send()` bis zum ACK oder Timeout blockiert. Nach `pollRadio()`
  gilt das nicht mehr, deshalb setzt der Pairing-Übergang nur ein Flag
  (`g_flush_requested`), das der nächste Durchlauf abarbeitet.

**Regel 6.3 — Dateiformat & Ort.**
* Pfad: `APP_DATA_PATH("eeprom.bin")` → wird zu `/data/eeprom.bin` und vom
  Storage-Service auf `/ext/apps_data/<appid>/` gemappt.
* **Kein `storage_common_mkdir()`**: `APP_DATA_PATH("")` wäre `/data/` und damit
  ein Pfad mit leerem Namen, den die Storage-API ablehnt. Das Verzeichnis legt
  der Storage-Service beim Auflösen des `/data`-Alias selbst an
  (`storage_process_alias(..., create_dir=true)` im Öffnen-Pfad).
* Header vor den Nutzdaten: Magic (`0x38435248`, auf Datei die Bytefolge
  `"HRC8"`), Version (`uint8_t`), Payload-Größe (`uint16_t`), Device-ID (3 B),
  Serial (10 B), CRC32 über **Identität + Nutzdaten**. Bei Mismatch: Datei
  verwerfen, Shadow mit `0xFF` initialisieren (AVR-EEPROM-Semantik!) — AskSin++
  erkennt daran den Auslieferungszustand und legt seine Defaults an.
* Schreiben **so atomar wie FatFS es zulässt**: nach `eeprom.bin.tmp` schreiben,
  alte Datei löschen, umbenennen. `f_rename` kann ein existierendes Ziel nicht
  überschreiben, das Löschen ist also unvermeidbar. Für das Fenster dazwischen
  liest `load()` als Rückfallebene die `.tmp` — sonst kostet ein Stromausfall
  genau dort das angelernte Gerät.

**Regel 6.4 — Device-ID/Serial sind persistent, nicht zufällig pro Start.**
HMID und Seriennummer werden aus der **STM32-UID des Flipper** abgeleitet
(`furi_hal_version_uid()`), nicht aus einem Zufallsgenerator, und im Dateikopf
persistiert. Zwei Gründe: reproduzierbar (geht `eeprom.bin` verloren, bekommt das
Gerät dieselbe HMID zurück, statt in der Zentrale als Duplikat aufzutauchen) und
netzweit eindeutig (zwei Flipper kollidieren nicht). `00 00 00` ist als eigene
HMID verboten — das ist die Broadcast-Adresse, `HMID::valid()` wäre false.

Die Werte liegen bewusst im **Dateikopf und nicht im AskSin-Shadow**: den
verwaltet `StorageWrapper::setup()` und initialisiert ihn bei jeder Änderung des
Registerlayouts (checksum-Mismatch) neu. Die Identität muss das überleben.

`hm_service.cpp::devinfo` ist deshalb **nicht `const`** und wird in
`hm_service_init()` gefüllt, **bevor** `g_dev` konstruiert wird.

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
3. **Radio-Thread (eigener `FuriThread`, Priorität `FuriThreadPriorityNormal`):**
   besitzt die gesamte AskSin++-Welt exklusiv. Läuft als Loop:
   `Kommando-Queue mit Timeout 10 ms pollen → Kommando auf AskSin++ anwenden →
   EEPROM-Flush fällig? → sysclock nachziehen → hal.runready() → pollRadio() →
   Statusstruktur aktualisieren`.

   *Korrektur zur ursprünglichen Prämisse:* Hier stand „Priorität > Normal",
   damit das ACK-Fenster nicht auf einen Bildaufbau wartet. Auf der Hardware
   zeigte sich das Gegenteil — der CC1101 teilt sich den internen SPI-Bus mit dem
   Display, und ein höher priorisierter Pollthread entzieht dem GUI-Service den
   Bus („ViewPort lockup"). Aus demselben Grund liegt der Polltakt bei 10 ms und
   nicht bei 2 ms: 500 SPI-Transaktionen pro Sekunde nur für
   `rx_pipe_not_empty()` hat der Bus nicht vertragen. Die BidCos-Timings brauchen
   beides nicht — die Systemuhr läuft mit 10 ms, empfangene Telegramme warten im
   RX-FIFO, und das ACK-Fenster ist ~200 ms breit.

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
  (unpaired / pairing / paired mit HMID), letzte TX-Quittierung
  (`...` / `ACK` / `NAK` / `T/O` / `RGN`), RSSI des letzten Empfangs in dBm.
* Der Weg der TX-Quittierung ins UI ist nicht offensichtlich: `Device::send()`
  gibt sein Ergebnis nur als `bool` an `RemoteChannel::state()` zurück — und das
  ist `void`. Deshalb schneidet **`FlipperRadio` die Quittung selbst mit**
  (`write()` merkt Zähler und Ziel, `read()` erkennt das passende ACK/NACK) und
  `hm_service` liest sie nach dem Tastendruck über `txResult()` ab.
  Retry-Werte werden dafür **nicht** angefasst (§11).
* Texte kurz halten: `FontSecondary` passt bei 128 px nur rund 25 Zeichen.
* Steuerung: Steuerkreuz wählt den Kanal, OK sendet. Kurzer vs. langer Druck des
  OK-Buttons wird über `InputTypeShort` / `InputTypeLong` auf AskSin++
  short-/long-press gemappt.
* `InputTypeRepeat` bei gehaltenem OK → AskSin++ erwartet für Longpress
  **wiederholte** Telegramme. Der Zähler (`RemoteChannel::repeatcnt`) bleibt
  dabei **gleich** und wird erst beim Loslassen inkrementiert — deshalb die vier
  Zustände in `HmButtonEvent`. Dieses Detail ist der häufigste Grund, warum
  Longpress an der CCU nicht ankommt.
* Back-Taste: **kurz = App beenden** (mit Flush, siehe §6.2), **lang = anlernen**.

  *Korrektur zur ursprünglichen Prämisse:* Hier stand die umgekehrte Belegung.
  Sie geht auf einem Ein-Bildschirm-Gerät nicht auf — es gibt kein „zurück", und
  ein langer Druck zum Beenden kollidiert mit der Erwartung, dass BACK sofort
  schließt. Das Anlernen ist die seltenere Aktion und bekommt deshalb den langen
  Druck. Ein langer BACK-Druck löst kein `InputTypeShort` aus, ein
  versehentliches Beenden ist also ausgeschlossen.

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
* **Der Shim ist im Normalbetrieb tot.** `fbt` übersetzt FAPs mit `-DNDEBUG`, und
  `upstream/Debug.h` macht daraus leere Makros — AskSin++ schweigt vollständig,
  `FlipperSerial` wird nie aufgerufen. Sichtbar wird der Funkverkehr nur über
  `HMRC8_TRACE_TELEGRAMS` (eigene Hex-Dumps in `flipper_radio.cpp`).
  Wer zusätzlich die AskSin-Statemachine (Pairing, Config, Peers) sehen will,
  kommentiert in `application.fam` die `"-UNDEBUG"`-Zeile der Lib-`cflags` ein.
  **Nur zur Fehlersuche** — `DPRINT` feuert dann auch mitten im ACK-Fenster
  (`Device::send()` ruft `msg.dump()`) und verletzt Regel 8.1. `FURI_NDEBUG`
  bleibt davon unberührt, `furi_assert()` ändert sein Verhalten also nicht.
* **Regel 8.1 — Kein Logging im Funk-Zeitfenster.** Zwischen TX und ACK-Empfang
  wird nicht geloggt; Ereignisse werden in einen Ringpuffer geschrieben und danach
  ausgegeben. UART-Logging verzögert im Millisekundenbereich und zerstört
  BidCos-Timings.
  Umgesetzt als `as::RadioTrace` in `port/flipper_radio.h`: `write()`/`read()`
  rufen nur noch `RadioTrace::frame()` bzw. `RadioTrace::note()` (allokationsfrei,
  16 Plätze, älteste fallen raus). Ausgegeben wird mit `RadioTrace::drain()` am
  Anfang von `hm_service_poll()` sowie nach `hm_service_button()` und
  `hm_service_start_pairing()` — dort ist `Device::send()` fertig und die Schleife
  garantiert funkfrei, genau wie beim EEPROM-Flush (§6.2).
  `RadioTrace::note()` merkt sich nur den **Zeiger** auf den Text: ausschließlich
  Stringliterale übergeben, niemals einen Stackpuffer.
* Telegramm-Dumps (hex) nur hinter einem `#define HMRC8_TRACE_TELEGRAMS`, per
  Default aus.
* Log lesen: `ufbt cli` → `log`.

---

## 9. AskSin++-Gerätedefinition (8-Tasten-Fernbedienung, HM-RC-X)

**Regel 9.1 — `DeviceInfo` wird nicht erfunden.** Die Struktur ist (Feldnamen aus
`upstream/Device.h:78`): `DeviceID[3]`, **`Serial[11]`** (10 Zeichen + NUL),
`DeviceModel[2]`, `Firmware`, `DeviceType`, `DeviceInfo[2]`.
Model-ID, Firmware-Version und Info-Bytes **müssen exakt zu einem Eintrag in den
`rftypes`-XMLs der Zielzentrale passen** (RaspberryMatic: `/opt/HomeMatic/etc/config/rftypes/`).
Passt die Kombination nicht, akzeptiert die CCU das Anlernen entweder gar nicht
oder legt ein Gerät mit falscher Kanalanzahl an.
→ Diese Werte werden **aus der Zentrale ausgelesen und im Code mit Quellenangabe
kommentiert**, nicht aus dem Gedächtnis eingesetzt.

*Korrektur zur ursprünglichen Prämisse:* Es gibt in der Zielfirmware **kein**
`rf_rc_8.xml`. Stattdessen matcht `rf_rc.xml` einen generischen Typ, der genau
dafür gemacht ist:

```xml
<type name="Generic RC" id="HM-RC-X" priority="1">
  <parameter index="22.0" size="1.0" const_value="0x40"/></type>
<channel index="1" type="KEY" count_from_sysinfo="23.0:1.0" .../>
```

Index 22 des DEVINFO-Telegramms ist das DeviceType-Byte, Index 23 die Kanalzahl —
beides bedient AskSin++ von selbst (`DeviceType::Remote == 0x40`;
`getDeviceInfo()` schreibt `this->channels()`). Die Modell-ID muss dafür nur
*keinem* `priority`-2-Eintrag entsprechen; `0xF001` liegt weit außerhalb der
eQ-3-Vergabe. Details und Fallstricke im Kopf von `hm_service.cpp`.

**Regel 9.2 — Serial ist eindeutig.** 10 Zeichen, muss über alle Geräte im
Homematic-Netz eindeutig sein. Wird bei Erstinitialisierung aus der
Flipper-Hardware-UID abgeleitet und persistiert (§6.4). Schema wie bei eQ-3:
drei Großbuchstaben + sieben Ziffern (`FLP0123456`).

**Regel 9.3 — Kanalstruktur.** `MultiChannelDevice` mit 8 `RemoteChannel`-Instanzen,
`List0` für Geräteregister, `List4` für Peer-Register. Peer-Anzahl pro Kanal
konservativ (aktuell 6) — jeder Peer-Slot kostet EEPROM-Shadow (§6.1).
Belegung ab `EEPROM_START_ADDR = 0x20`, insgesamt rund 300 der 1024 Byte.

**Regel 9.4 — Pairing-Modus.** Config-Taste des Originalgeräts wird als
UI-Aktion nachgebildet (langer Druck auf BACK, §7.3). Der
Anlernvorgang läuft ausschließlich über die reguläre AskSin++-Pairing-Logik
(`sdev.startPairing()`); es wird **keine** HMID der Zentrale hardcodiert und
kein Pairing-Zustand manuell in den EEPROM-Shadow geschrieben.
`startPairing()` sendet **ein einzelnes** DEVINFO und hält das Gerät 20 s wach —
es ist kein Dauerzustand. Die UI zeigt „Anlernen läuft" deshalb nur
`ASKSIN_PAIRING_TIMEOUT_MS` lang und fällt danach auf „nicht angelernt" zurück,
statt für immer stehen zu bleiben.

**Regel 9.5 — AES/Signing ist aktiv.**

*Korrektur zur ursprünglichen Prämisse:* Hier stand „für Meilenstein 1
deaktiviert, eigener Meilenstein". Das ging nicht auf: die CCU setzt beim
Anlernen auf **jedem** Kanal `AES_ACTIVE` (List1, Register `0x08`, siehe
`upstream/Register.h:42`) selbst auf `0x01` und beantwortet danach jeden
Tastendruck mit einer Challenge statt mit einem ACK. Ohne Signatur kommt kein
einziger Tastendruck an — Signing ist damit keine Kür, sondern Voraussetzung
für Meilenstein 1. Der Nutzer hat den Meilenstein deshalb vorgezogen.

Umgesetzt über die Upstream-Mechanik, ohne eigene Krypto:
* `USE_AES`, `HM_DEF_KEY`, `HM_DEF_KEY_INDEX` stehen in `application.fam`
  (§4.5 — `USE_AES` ändert über `KeyStore::size()` das Speicherlayout).
* `upstream/aes.cpp` ist in der Quellenliste. Der einzige Grund für den
  früheren Ausschluss — ungeguardetes `#include <avr/pgmspace.h>` — ist über
  den Shim `port/arduino_compat/avr/pgmspace.h` erledigt, **ohne Patch**.
* Den Ablauf macht AskSin++ selbst: `Device::send()` erkennt die Challenge und
  ruft `processChallenge()`; `MultiChannelDevice::validSignature()` fordert
  umgekehrt Signaturen der Zentrale an.

**Regel 9.5.1 — Eine AES-Challenge ist weder ACK noch NACK.** Sie ist
`AS_MESSAGE_RESPONSE` (`0x02`) mit `comm == AS_RESPONSE_AES_CHALLANGE`
(`0x04`). `Message::isAck()` prüft nur `(comm & AS_RESPONSE_NACK) ==
AS_RESPONSE_ACK`, also `(0x04 & 0x80) == 0x00` — und das ist **true**. Wer die
Quittung mitschneidet, muss `isChallengeAes()` deshalb **vor** `isAck()`
abfragen, sonst meldet die App einen erfolgreichen Sendevorgang, während die
Zentrale noch auf die Signatur wartet.

**Regel 9.5.2 — Der Schlüssel wird nicht geraten.** `HM_DEF_KEY` ist derzeit
der Beispielwert aus `upstream/README.md`, **nicht** der verifizierte
eQ-3-Werksschlüssel; der ist laut FHEM-Wiki („AES Encryption") in allen
HM-Geräten gleich, wird dort aber nicht veröffentlicht. Bis der richtige Wert
bekannt ist, bleibt er zur Laufzeit überschreibbar über
`/ext/apps_data/hm_rc8/aes_key.txt` (`FlipperEeprom::loadAesKey()`). Der
Schlüsselindex aus der Challenge ist das **Doppelte** der Schlüsselnummer:
Index `0` heißt Auslieferungsschlüssel, ein Wert > 0 heißt, die Zentrale hat
einen eigenen System-Sicherheitsschlüssel.

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
* **Register `0x00` in die Preset-Tabelle aufnehmen** — beendet die Liste vorzeitig
  und macht das komplette Preset wirkungslos, ohne jede Fehlermeldung (§5.3.2a).
* **Nach `reset()` / `load_custom_preset()` den GDO0-Pinmodus nicht wiederherstellen**
  (§5.2.1).
* Nach TX blind `flush_rx()` aufrufen (§5.5.3) oder RSSI erst nach dem Flush
  lesen (§5.4.1).
* **`rx_pipe_not_empty()` als „Telegramm vollständig" deuten und ohne
  GDO0-Warten `flush_rx()` rufen** — löscht das gerade einlaufende Paket
  (§5.4.3). Das war die Ursache dafür, dass sich das Gerät nicht anlernen ließ.
* Den Empfänger nach einem CRC-Fehler nicht neu scharf stellen (§5.4.4).
* Im Funk-Zeitfenster direkt loggen statt über den Ringpuffer (§8.1) — betrifft
  insbesondere die Telegramm-Dumps in `read()`/`write()`.
* Pro Byte auf die SD-Karte schreiben.
* `std::vector`/`std::string`/`std::function` oder irgendetwas Exception-basiertes.
* Globale C++-Objekte mit nicht-trivialen Konstruktoren.
* Async-/Raw-SubGhz-Modi für BidCos verwenden.
* Blockierende Operationen (Storage, Log, `delay`) im ACK-Zeitfenster.
* **`Message::isAck()` vor `isChallengeAes()` prüfen** — eine AES-Challenge
  wird dadurch als erfolgreiche Quittung verbucht (§9.5.1).
* **`view_port_update()` bedingungslos in jedem Schleifendurchlauf aufrufen.**
  Der ViewPort-Mutex wird dort nur 2 ms lang angefragt
  (`view_port.c:187`), und der Draw-Callback hält ihn länger — das erzeugt die
  Warnung „ViewPort lockup". Nur bei tatsächlich geändertem Modell auffrischen.
* Einen AES-Schlüssel raten (§9.5.2).
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

**Verifiziert (gebaut, `ufbt` grün ohne Warnungen, Target 7 / API 87.1):**

| Baustein | Datei | Status |
|---|---|---|
| Arduino-/AVR-Shim | `port/arduino_compat/` | fertig |
| Debug → `FURI_LOG` | `arduino_compat/Arduino.h` | fertig, zeilengepuffert; per Default inaktiv (§8) |
| Systemuhr | `port/flipper_sysclock.h` | fertig |
| EEPROM-Ersatz | `port/flipper_eeprom.*` | fertig, CRC + `.tmp`-Recovery + Identität |
| Radio (CC1101) | `port/flipper_radio.*` | fertig, **auf HW noch nicht erneut getestet** |
| C++-Runtime | `port/cxx_runtime.cpp` | fertig |
| Fassade | `port/hm_service.*` | fertig |
| HM-RC-X-Gerät | `port/hm_service.cpp` | fertig, 8 Kanäle |
| GUI | `src/ui/hm_view.*` | fertig |

**Angewandte Patches:** `patches/001-radio-flipper-port.patch` — schneidet in
`upstream/Radio.h` den kompletten AVR-Funkstack heraus (eigene SPI-Klassen,
GDO0-ISR, Treiber für CC1101/Si4431/RFM69). Nötig, weil `Radio-Si4431.h:172` ein
`WRITE_REG` definiert, das mit CMSIS (`stm32wbxx.h`) kollidiert und dadurch jeden
STM32-LL-Header zerlegt. Ein Shim reicht hier nicht: `Device.h:15` inkludiert
`"Radio.h"` in Anführungszeichen, quoted-Includes werden zuerst relativ zum
inkludierenden File aufgelöst — `upstream/Radio.h` gewinnt also immer.
Genau eine Upstream-Datei ist verändert.

**Gegen `firmware.elf` 1.4.3 disassembliert und damit erledigt:**
* Preset-Layout (Paare + `0,0` + 8 PATABLE-Bytes) — `@0x0800f734`.
  Dabei fiel die Terminator-Falle bei Register `0x00` auf (§5.3.2a) — sie hat
  bis dahin **das gesamte Preset wirkungslos gemacht**.
* `furi_hal_subghz_reset()` legt GDO0 auf `GpioModeAnalog` — `@0x0800f6f4`
  (§5.2.1).
* `cc1101_read_fifo` klemmt auf 64 Byte — `@0x0801d3c8` (§5.4.2).
* `furi_hal_subghz_write_packet()` schreibt das Längenbyte selbst — `@0x0800f5c4`.
  `Message::buffer()` (= `&cnt`) mit `length()` zu übergeben ist also korrekt.
* PATABLE `0xC0` ist **kein** eigener Wert, sondern `PA_MaxPower` aus
  `Radio-CC1101.h:409` — die `0x03` in `initVal[]` wird dort direkt danach
  überschrieben. Identisch mit dem GFSK-9,99-kb-Preset der Firmware.

**Gerätefirmware weicht vom SDK ab (Stand 2026-09-21).** Auf dem Testgerät
läuft **Unleashed `unlshd-086`, API 87.6**, nicht die offizielle 1.4.3 / 87.1,
gegen die das ufbt-SDK gebaut wird. Gegen den Unleashed-Quelltext geprüft
(`targets/f7/furi_hal/furi_hal_subghz.c`): `load_custom_preset`,
`load_registers`, `reset`, `tx`, `rx`, `idle`, `write_packet`, `read_packet`,
`rx_pipe_not_empty`, `is_rx_data_crc_valid` sind **byteweise identisch** zur
offiziellen Fassung. Die Terminator-Falle (§5.3.2a), das Zurücksetzen von GDO0
auf `GpioModeAnalog` (§5.2.1) und die 64-Byte-Klemme (§5.4.2) gelten dort
genauso. **Für den Funkpfad allein wäre ein SDK-Umstieg also nicht nötig
gewesen.**

**Trotzdem wird ab 2026-09-21 gegen das Unleashed-SDK gebaut** (`unlshd-086`,
API 87.6, isoliertes `UFBT_HOME=~/.ufbt-unleashed`; das offizielle SDK in
`~/.ufbt` bleibt unangetastet). Grund ist ein Befund an einer anderen FAP auf
derselben Hardware: `FURI_HAL_VERSION_DEVICE_NAME_LENGTH` ist offiziell
`1+8+9 = 18`, bei Unleashed `1+20 = 21`. Dadurch verschiebt sich in
`GapConfig` das Feld `conn_param` um 4 Byte, und ein mit dem offiziellen SDK
gebautes FAP schreibt der Firmware dort Müll hin. Das kostete dort die
komplette BLE-Kopplung — ohne Fehlermeldung, ohne Absturz.

Die Lehre ist allgemein und gilt auch hier: **Die Prüfung „ist die benutzte
Funktion in `api_symbols.csv` mit `+` markiert" reicht nicht.** Sie sagt nur,
dass das Symbol auflösbar ist, nichts über das Speicherlayout der Strukturen,
die man der Firmware übergibt. Solange Geräte-Firmware und SDK
auseinanderlaufen, ist jede struct an der FAP-Grenze eine stille Fehlerquelle.
Deshalb: SDK == Geräte-Firmware, auch wenn der aktuell benutzte Ausschnitt
identisch aussieht.

Einziger relevanter Unterschied im Funkpfad: Unleashed ersetzt die Regionsprüfung in
`set_frequency()` durch `furi_hal_subghz_is_tx_allowed()` ohne Regionsbezug —
868,3 MHz ist dort immer sendefähig, `furi_hal_subghz_tx()` kann auf diesem
Gerät nicht regionsbedingt false liefern. Die Auswertung bleibt trotzdem
stehen (§5.5), damit die App auf offizieller Firmware nicht stumm scheitert.

**Behobene Ursache des fehlgeschlagenen Anlernens (2026-09-21, ungetestet):**
`FlipperRadio::read()` wertete den RX-FIFO aus, sobald
`rx_pipe_not_empty()` true meldete — also mitten im laufenden Empfang — und
warf das einlaufende Telegramm per `flush_rx()` weg (§5.4.3). Dazu kam, dass
ein per CRC-Autoflush verworfenes Paket den Chip dauerhaft in IDLE zurückließ
(§5.4.4). Beides zusammen macht den Empfang praktisch unmöglich; gesendet wurde
dagegen korrekt. Behoben in `port/flipper_radio.cpp`
(`waitRxComplete()`, `keepRxAlive()`).

**Offene `TODO(verify)` im Code:** keine mehr.
* Erledigt: die RSSI-Umrechnung. `FlipperRadio::rssi()` liefert jetzt die
  AskSin++-Skala (**Betrag** des Pegels in dBm, 60 == −60 dBm) statt des
  CC1101-Rohwerts. Quelle: `Radio-CC1101.h:469`, `calculateRSSI()`.
  `hm_rssi_to_dbm()` in `hm_service.h` wurde mitgezogen.

**Nächster Schritt: Test gegen die echte CCU.** Seit der Preset-Korrektur und
der Empfangskorrektur ist kein Hardwarelauf erfolgt — alles hier ist damit
ungetestet im Sinne von §10.5. Zu beobachten: taucht das Gerät im Anlernmodus
als „HM-RC-X" mit acht KEY-Kanälen auf, kommen ACKs (`ACK` statt `T/O` in der
Statuszeile), überlebt das Pairing einen Neustart. Die Diagnosezeile
`diag tx=… rx=… crcerr=… busy=… oversz=… gdo0=…` im Log ordnet einen
Fehlschlag ein, ohne den Funkverkehr lesen zu müssen.

**Auf Hardware verifiziert (Hardwaretest gegen eine echte eQ-3-CCU):**
* Empfangskorrektur wirkt: `crcerr=0`, `oversz=0` über längere Laufzeit,
  `busy` zählt hoch. `MaxDataLen = 17` reicht — der Punkt ist damit erledigt.
* **Das Anlernen läuft vollständig durch.** Die CCU fährt die
  Konfigurationssequenz über alle acht Kanäle, das Gerät antwortet korrekt und
  erscheint in der WebUI als Gerät mit acht Kanälen. Das Pairing wird
  persistiert.
* Damit ist die Gerätekennung (`0xF001`, generischer `HM-RC-X`) **belegt** und
  wird nicht mehr angefasst. Der frühere „erste Stellknopf" ist erledigt.

**Weiterhin bewusst offen:**
* Der Wert von `HM_DEF_KEY` ist unverifiziert (§9.5.2). Solange er nicht
  stimmt, schlägt jede Signatur fehl — und da `validSignature()` auch für
  eingehende Konfigurationstelegramme greift, kann dann auch ein erneutes
  Anlernen scheitern. Das ist die einzige offene Abhängigkeit.

---

## 13. Arbeitsweise in diesem Projekt

* Vor einer Änderung: relevante Header aus `~/.ufbt/current/sdk_headers/` lesen.
* Änderungen klein halten und pro Schicht (Shim / Radio / Storage / Device / UI)
  getrennt liefern, damit sie einzeln auf Hardware verifizierbar sind.
* Bei Konflikt zwischen „schnell zum Laufen bringen" und „Protokoll korrekt":
  Protokoll gewinnt. Ein Gerät, das sich anlernt aber sporadisch ACKs verliert,
  ist schlimmer als eines, das gar nicht anlernt.
* Unsicherheit wird benannt, nicht überspielt.
