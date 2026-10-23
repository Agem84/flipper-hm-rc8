# Fremde Bestandteile und Namensnennung

Dieses Repository enthaelt eigenen Code **und** bindet fremde Projekte ein.
Die folgende Aufstellung ist Teil der Namensnennungspflicht aus
[`LICENSE`](LICENSE) (CC BY-NC-SA 3.0 DE) und muss bei Weitergabe oder
Bearbeitung erhalten bleiben.

Was die Lizenz abdeckt und was nicht, steht in
[LICENSE-HINWEIS.md](LICENSE-HINWEIS.md).

---

## AskSin++ — die eigentliche Protokollimplementierung

|  |  |
|---|---|
| Projekt | **AskSin++** |
| Urheber | **pa-pa** und Mitwirkende |
| Quelle | <https://github.com/pa-pa/AskSinPP> |
| Lizenz | **CC BY-NC-SA 3.0 DE** — <https://creativecommons.org/licenses/by-nc-sa/3.0/de/> |
| Lizenzvermerk | im Kopf jeder Upstream-Datei, z. B. `upstream/AskSinPP.h`. Nachgeprueft ueber alle `.h`/`.cpp` ausserhalb von `doc/` und `examples/`: einheitlich dieselbe Lizenz, keine Ausnahme — bis auf `aes.*`, siehe unten. |
| Eingebunden als | Git-Submodul unter `lib/asksinpp/upstream`, fest auf Commit `21bab8b92f7982c29beca08ff3c0ad6ef1e72a5c` |
| Version | AskSin++ 5.0.3 (`ASKSIN_PLUS_PLUS_MAJOR/MINOR/SERVICE`) |

**AskSin++ ist das Herzstueck dieses Projekts.** Das BidCos-Protokoll, die
Geraete- und Kanalmodelle, der Anlernvorgang, die Register- und Peerverwaltung
und die AES-Signatur stammen vollstaendig von dort. Dieses Repository liefert
"nur" die Flipper-Portierung: eine Hardwareanbindung an den internen CC1101,
einen EEPROM-Ersatz, einen Arduino-Ersatz und eine Benutzeroberflaeche.

**Der Upstream-Code liegt nicht in diesem Repository.** Ein Submodul speichert
ausschliesslich einen Verweis auf Projekt und Commit; geholt wird der Code bei
`git submodule update` direkt vom Urheber. Damit wird AskSin++ hier weder
weiterverbreitet noch veraendert.

### Aenderungen an AskSin++

Upstream wird **nie direkt editiert**. Jede noetige Aenderung liegt als
nummerierter Patch in `lib/asksinpp/patches/` und wird erst beim Einrichten
auf den Arbeitsbaum angewandt (`scripts/setup.sh` / `scripts/setup.ps1`).
Derzeit ist das genau eine Aenderung an genau einer Datei:

| Patch | Datei | Zweck |
|---|---|---|
| `001-radio-flipper-port.patch` | `Radio.h` | Schneidet den AVR-Funkstack (eigene SPI-Klassen, GDO0-ISR, Treiber fuer CC1101/Si4431/RFM69) hinter `ASKSIN_FLIPPER_PORT` heraus. Noetig, weil `Radio-Si4431.h` ein `WRITE_REG` definiert, das mit CMSIS (`stm32wbxx.h`) kollidiert und jeden STM32-LL-Header zerlegt. Ein Shim genuegt nicht: `Device.h` inkludiert `"Radio.h"` in Anfuehrungszeichen, und quoted-Includes werden zuerst relativ zur inkludierenden Datei aufgeloest — `upstream/Radio.h` gewinnt immer. |

Daraus folgt auch: `git status` meldet fuer das Submodul dauerhaft
`modified content`. Das ist beabsichtigt und in `.gitmodules` per
`ignore = dirty` ruhiggestellt.

### AVR-Crypto-Lib (in AskSin++ als `aes.h` / `aes.cpp`)

|  |  |
|---|---|
| Urheber | **Daniel Otte**, Copyright (C) 2008 — fuer AskSin++ angepasst von *trilu* |
| Lizenzvermerk | `upstream/aes.h`, `upstream/aes.cpp`: *"adoption of Daniel Otte's cool AVR-Crypto-Lib, Copyright (C) 2008, GPLv3 or later"* |
| Lizenz | **GPL-3.0-or-later**, zusaetzlich zum CC-BY-NC-SA-Vermerk von AskSin++ in derselben Datei |
| Eingebunden | nicht im Repository; `upstream/aes.cpp` ist aber Teil des Builds (`USE_AES` in `application.fam`) |

Das sind die **einzigen zwei** Dateien im gesamten Upstream mit einem
GPL-Vermerk, und sie tragen zwei Lizenzvermerke gleichzeitig. Fuer dieses
Repository ist das folgenlos — es enthaelt keine der beiden Dateien. Fuer die
Weitergabe eines **gebauten** `.fap` ist es nicht folgenlos: GPLv3 und das
Nicht-kommerziell-Gebot von CC BY-NC-SA sind nicht miteinander vereinbar.
Deshalb liegt hier kein vorgebautes Binaer bei. Einordnung in
[LICENSE-HINWEIS.md](LICENSE-HINWEIS.md).

---

## Flipper Zero Firmware / SDK

|  |  |
|---|---|
| Projekt | Flipper Zero Firmware, bzw. **Unleashed** als verwendeter Zweig |
| Quelle | <https://github.com/flipperdevices/flipperzero-firmware> · <https://github.com/DarkFlippers/unleashed-firmware> |
| Lizenz | GPL-3.0 |
| Eingebunden | **nicht.** Headers und Symboltabelle kommen zur Bauzeit aus dem von `ufbt` geladenen SDK-Archiv. |

Dieses Projekt baut gegen die oeffentliche FAP-Schnittstelle und linkt keinen
Firmware-Quelltext in die App. Es wird kein Firmware-Code weiterverbreitet.

---

## Marken

* **Homematic**, **BidCos**, **HM-RC-8** — Marken bzw. Produktbezeichnungen der
  **eQ-3 AG**. Dieses Projekt ist inoffiziell, unabhaengig und nicht von eQ-3
  autorisiert oder unterstuetzt.
* **Flipper Zero** — Marke von **Flipper Devices Inc.** Ebenfalls keine
  Verbindung.
* **RaspberryMatic** — <https://raspberrymatic.de/>, erwaehnt als Zielzentrale.

## Quellen fuer Protokollwissen

Die in `CLAUDE.md` dokumentierten Protokolldetails stammen aus dem Quelltext
von AskSin++, aus dem **FHEM-Wiki** (insbesondere der Artikel zu
"AES Encryption") und aus den `rftypes`-XML-Dateien der Zielzentrale. Der
eQ-3-Werksschluessel wurde **nicht** uebernommen und ist in diesem Projekt
nicht enthalten (siehe README, Abschnitt "AES-Signatur").
