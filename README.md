# Homematic RC-8 für Flipper Zero

[![build](https://github.com/Agem84/flipper-hm-rc8/actions/workflows/build.yml/badge.svg)](https://github.com/Agem84/flipper-hm-rc8/actions/workflows/build.yml)
[![Lizenz: CC BY-NC-SA 3.0 DE](https://img.shields.io/badge/Lizenz-CC%20BY--NC--SA%203.0%20DE-lightgrey.svg)](LICENSE)

Eine Flipper-Zero-App (FAP), die über den **internen CC1101** als
**Homematic-BidCos-Fernbedienung** auftritt: Sie lässt sich regulär über den
Anlernmodus einer CCU bzw. RaspberryMatic anlernen und sendet danach
Tastendrücke als BidCos-Telegramme — acht Kanäle, kurzer und langer Druck.

Die Protokollarbeit macht die portierte Bibliothek **[AskSin++](https://github.com/pa-pa/AskSinPP)**
von *pa-pa*. Dieses Projekt ist die Flipper-Portierung drumherum: Funkanbindung,
EEPROM-Ersatz, Arduino-Ersatz, Bedienoberfläche. Siehe [NOTICE.md](NOTICE.md).

> 🇬🇧 **English:** see [README.en.md](README.en.md). The German documents are
> the authoritative ones — Homematic is largely a German-speaking ecosystem.

---

## Stand

| | |
|---|---|
| **Anlernen an einer echten eQ-3-CCU** | ✅ auf Hardware verifiziert — die Zentrale fährt die Konfigurationssequenz über alle acht Kanäle, das Gerät erscheint mit acht Kanälen in der WebUI, das Pairing überlebt einen Neustart |
| **Tastendrücke (short/long)** | ⚠️ abhängig vom AES-Schlüssel, siehe unten |
| **AES-Werksschlüssel** | ❌ **unverifiziert** — der eingebaute Wert ist ein Beispielwert, nicht der echte eQ-3-Werksschlüssel |

**Der offene Punkt im Klartext:** Die CCU setzt beim Anlernen auf jedem Kanal
`AES_ACTIVE` und beantwortet danach jeden Tastendruck mit einer
AES-Challenge statt mit einem ACK. Ohne passenden Schlüssel kommt der
Tastendruck nicht an. Der verwendete `HM_DEF_KEY` ist der Beispielwert aus der
AskSin++-Dokumentation; der echte eQ-3-Werksschlüssel ist in allen
HM-Geräten gleich, wird aber nirgends veröffentlicht — und wurde hier
bewusst **nicht geraten**. Der Schlüssel ist zur Laufzeit ersetzbar, ohne neu
zu bauen (siehe [AES-Signatur](#aes-signatur)).

Alles Weitere zum Umsetzungsstand steht in [CLAUDE.md](CLAUDE.md) §12a.

---

## Voraussetzungen

| | |
|---|---|
| Gerät | Flipper Zero (Target `f7`), interner CC1101 — **kein** externes Funkmodul nötig |
| Firmware auf dem Gerät | **Unleashed `unlshd-086`**, API **87.6** |
| Build-Tool | [`ufbt`](https://pypi.org/project/ufbt/) (Python-Wheel) |
| Zentrale | CCU2/CCU3 oder RaspberryMatic, erreichbar auf 868,3 MHz |
| Für den Build | `git` (wegen des Submoduls) |

### SDK und Geräte-Firmware müssen zusammenpassen

Das ist hier keine Stilfrage. Ein FAP erhält Strukturgrößen aus den
SDK-Headern; läuft es gegen eine Firmware mit anderem Layout, schreibt es der
Firmware Werte an die falsche Stelle. Das passiert **ohne Fehlermeldung und
ohne Absturz** — der Build ist grün, und der Fehler fällt erst auf dem Funk
auf.

Ein belegtes Beispiel aus genau diesen beiden Firmwares:
`FURI_HAL_VERSION_DEVICE_NAME_LENGTH` ist offiziell 18, bei Unleashed 21. Jede
Struktur, in der dieses Makro steckt, hat damit auf den beiden Firmwares eine
**andere Größe** — ein FAP, das so eine Struktur über die Firmware-Grenze
übergibt, beschreibt dann die falschen Offsets. Der Compiler kann das nicht
sehen, weil er nur die Header des SDK kennt.

Deshalb gilt: **auf dem Gerät dieselbe Firmware wie das Build-SDK.** Die
Begründung im Detail steht in [CLAUDE.md](CLAUDE.md) §12a.

---

## Einrichten und bauen

```bash
git clone --recurse-submodules https://github.com/Agem84/flipper-hm-rc8.git
cd flipper-hm-rc8
```

Dann die AskSin++-Patches anwenden — **ohne diesen Schritt bricht der Build ab:**

```bash
# Linux / macOS
./scripts/setup.sh
```

```powershell
# Windows / PowerShell
.\scripts\setup.ps1
```

Das Skript holt das Submodul (falls noch nicht geschehen) und wendet alle
Patches aus `lib/asksinpp/patches/` an. Es ist mehrfach aufrufbar: schon
angewandte Patches werden erkannt und übersprungen.

Anschließend bauen — mit dem **Unleashed-SDK** in einem eigenen `UFBT_HOME`,
damit ein daneben liegendes offizielles SDK unangetastet bleibt:

```bash
# Linux / macOS
UFBT_HOME=~/.ufbt-unleashed ufbt update --index-url=https://up.unleashedflip.com/directory.json
UFBT_HOME=~/.ufbt-unleashed ufbt
```

```powershell
# Windows / PowerShell
$env:UFBT_HOME = "$HOME\.ufbt-unleashed"
ufbt update --index-url=https://up.unleashedflip.com/directory.json
ufbt
```

> `ufbt update` holt die **neueste** Unleashed-Version. Das ist nicht
> zwangsläufig `unlshd-086`, gegen das dieses Projekt verifiziert ist. Exakt
> diesen Stand bekommt man mit
> `ufbt update --hw-target=f7 --url=https://unleashedflip.com/fw/unlshd-086/flipper-z-f7-sdk-unlshd-086.zip`
> (`--hw-target` ist bei einer direkten URL Pflicht — ohne Index kann `ufbt`
> das Ziel nicht aus dem Archiv ableiten)
> — genau das macht auch der CI-Lauf.

Aufs Gerät bringen und starten:

```bash
UFBT_HOME=~/.ufbt-unleashed ufbt launch
```

Log mitlesen: `ufbt cli`, dort `log`. Das fertige `.fap` liegt danach in
`dist/` und wird nicht im Repo geführt. Der CI-Lauf baut denselben Stand, legt
das Ergebnis aber bewusst nicht als Artefakt ab — siehe [Lizenz](#lizenz).

Einen Download gibt es **nicht** — selbst bauen ist hier der einzige Weg. Der
Grund ist eine Lizenzkollision im AES-Teil der Bibliothek, nicht Bequemlichkeit;
siehe [Lizenz](#lizenz). Das fertige `.fap` liegt nach dem Build in `dist/` und
gehört nach `/ext/apps/Sub-GHz/` auf die SD-Karte.

---

## Bedienung

Die App zeigt die acht Kanäle als 4×2-Raster, dazu Anlernstatus, Quittung des
letzten Sendevorgangs und den Empfangspegel.

| Taste | Wirkung |
|---|---|
| Steuerkreuz | Kanal 1–8 auswählen (begrenzend, kein Umlauf) |
| **OK** kurz | Tastendruck *short* auf dem gewählten Kanal |
| **OK** lang | Tastendruck *long* — Beginn, Wiederholungen beim Halten, Ende beim Loslassen |
| **BACK** kurz | App beenden (mit Speichern) |
| **BACK** lang | **Anlernen starten** |

Die Statuszeile quittiert jeden Sendevorgang mit `ACK`, `NAK`, `T/O`
(Zeitüberschreitung) oder `RGN` (Senden regionsbedingt gesperrt).

### Anlernen

1. An der Zentrale den Anlernmodus starten (CCU-WebUI → *Gerät anlernen*).
2. Auf dem Flipper **BACK lang** drücken. Die App sendet ein DEVINFO und
   bleibt 20 Sekunden ansprechbar; die Anzeige fällt danach auf „nicht
   angelernt" zurück, wenn nichts passiert ist.
3. Die Zentrale legt ein Gerät vom generischen Typ **HM-RC-X** mit acht
   KEY-Kanälen an und fährt die Konfiguration über alle Kanäle.

Die Geräte-ID (HMID) und die Seriennummer werden aus der STM32-UID des
Flippers abgeleitet und persistiert — geht die Zustandsdatei verloren, bekommt
das Gerät dieselbe HMID zurück und taucht in der Zentrale nicht als Duplikat
auf. Zwei verschiedene Flipper kollidieren nicht.

### Dateien auf dem Gerät

| Pfad | Inhalt |
|---|---|
| `/ext/apps_data/hm_rc8/eeprom.bin` | Anlernzustand: HMID, Seriennummer, Peers, Register. Löschen = Werkszustand. |
| `/ext/apps_data/hm_rc8/aes_key.txt` | Optionaler AES-Schlüssel, überschreibt den eingebauten Wert |

---

## AES-Signatur

Signieren ist bei dieser Geräteklasse **keine Kür, sondern Voraussetzung**:
Die Zentrale aktiviert `AES_ACTIVE` beim Anlernen selbst und verlangt danach
für jeden Tastendruck eine Signatur.

Der eingebaute Schlüssel ist, wie oben erklärt, unverifiziert. Statt ihn zu
erraten, lässt sich der richtige Wert zur Laufzeit hinterlegen:

```
/ext/apps_data/hm_rc8/aes_key.txt
```

16 Bytes als Hexadezimaltext. Zum Schlüsselindex: Der Index in der Challenge
ist das **Doppelte** der Schlüsselnummer — Index `0` heißt
Auslieferungsschlüssel, ein Wert größer 0 heißt, die Zentrale benutzt einen
eigenen System-Sicherheitsschlüssel.

> ⚠️ Diese Datei ist ein Geheimnis deiner Hausautomation. Sie ist in
> `.gitignore` ausgenommen — bitte nicht committen, nicht in Issues posten und
> nicht in Logausschnitte kopieren.

---

## Aufbau

```
hm_rc8/
├── application.fam            FAP-Manifest, dokumentiert jeden Schalter
├── icons/hm_rc8_10px.png      App-Symbol (10×10, 1 Bit)
├── src/                       REINER Flipper-Code — kein AskSin++-Include
│   ├── hm_rc8_app.cpp         Entry-Point, Eingabeschleife
│   ├── ui/                    ViewPort, Zeichnen, Modell-Snapshot
│   └── radio/                 Radio-Thread, Kommando-Queue
├── lib/asksinpp/
│   ├── upstream/              AskSin++ als Submodul (Fremdcode)
│   ├── patches/               nummerierte Patches gegen upstream/
│   └── port/                  die eigentliche Portierung
│       ├── flipper_radio.*    CC1101 im Packet-Mode
│       ├── flipper_eeprom.*   EEPROM-Ersatz: RAM-Shadow + Datei mit CRC
│       ├── arduino_compat/    Arduino.h-Ersatz, Debug → FURI_LOG
│       ├── flipper_sysclock.h Taktgeber für die AskSin++-Uhr
│       └── hm_service.*       Fassade — der einzige Zugang für src/
└── scripts/setup.*            Submodul holen, Patches anwenden
```

Drei Threads, streng getrennt: der GUI-Dienst zeichnet nur, der App-Thread
übersetzt Eingaben in Kommandos, und der Radio-Thread besitzt die komplette
AskSin++-Welt exklusiv. Kein AskSin++-Objekt wird jemals von außerhalb des
Radio-Threads angefasst.

**Die ausführliche Begründung jeder Entscheidung** — inklusive der
Disassemblierungen, mit denen Firmware-Verhalten belegt wurde — steht in
[CLAUDE.md](CLAUDE.md). Das ist kein Beiwerk: Wer hier etwas ändern will,
findet dort, warum Register `0x00` nicht ins Preset darf, warum der Empfänger
regelmäßig neu scharf gestellt werden muss und warum die Tickrate nicht
erhöht werden darf.

---

## Recht und Funkbetrieb

* 868,3 MHz ist ein **SRD-Band mit Duty-Cycle-Beschränkung** (in Deutschland
  typisch 1 % für 868,0–868,6 MHz). Die App hat bewusst **keine Dauersende-
  oder Flood-Funktion**; jeder Sendevorgang hängt an einer Benutzeraktion.
* Die App emuliert ein Gerät **im eigenen** Homematic-Netz. Es gibt keine
  Funktionen, die auf fremde Installationen zielen: kein Scannen fremder
  HMIDs, keine Rolling-Code-Angriffe, keine Übernahme angelernter
  Fremdgeräte.
* Die Regionssperre der Firmware wird **respektiert, nicht umgangen**. Lehnt
  die Firmware das Senden ab, zeigt die App `RGN` an.
* Der Betrieb von Funksendern unterliegt nationalem Recht. Die Verantwortung
  dafür liegt bei dir.

---

## Mitmachen

Issues und Pull Requests sind willkommen — bitte vorher
[CONTRIBUTING.md](CONTRIBUTING.md) lesen. Kurzfassung: `upstream/` wird nie
direkt editiert, Firmware-APIs werden gegen `api_symbols.csv` belegt statt aus
dem Gedächtnis zitiert, und „sollte funktionieren" ist kein Testergebnis.

Besonders gesucht: ein **verifizierter** Hardwaretest der Tastendrücke an einer
CCU — und damit die Antwort auf die Frage, ob der Schlüsselpfad stimmt.

---

## Lizenz

**CC BY-NC-SA 3.0 DE** — Namensnennung, nicht kommerziell, Weitergabe unter
gleichen Bedingungen. [`LICENSE`](LICENSE) enthält den deutschen Lizenztext im
Wortlaut; was darunter fällt und was nicht, steht in
[LICENSE-HINWEIS.md](LICENSE-HINWEIS.md).

Die Lizenz ist **nicht frei gewählt**: Der Portierungscode ist ein
abgeleitetes Werk von AskSin++, und dessen Weitergabe-unter-gleichen-
Bedingungen-Gebot gibt sie vor. MIT, Apache-2.0 und GPL sind damit
ausgeschlossen.

> ⚠️ **Es gibt bewusst kein vorgebautes `.fap`.** `upstream/aes.cpp` ist Teil
> des Builds und trägt neben dem CC-Vermerk einen zweiten: *GPLv3 or later*
> (AVR-Crypto-Lib von Daniel Otte). In einem gebauten Binär träfen damit GPLv3
> und das Nicht-kommerziell-Gebot aufeinander, und die beiden sind nicht
> miteinander vereinbar. Für den Selbstbau aus dem Quellcode ist das
> folgenlos. Einordnung in [LICENSE-HINWEIS.md](LICENSE-HINWEIS.md).

Dank an **pa-pa** und die Mitwirkenden an [AskSin++](https://github.com/pa-pa/AskSinPP)
— ohne diese Bibliothek gäbe es dieses Projekt nicht.

Homematic und BidCos sind Marken der eQ-3 AG, Flipper Zero ist eine Marke von
Flipper Devices Inc. Dieses Projekt ist inoffiziell und unabhängig.
