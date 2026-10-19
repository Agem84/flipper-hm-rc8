# Mitmachen

Danke für das Interesse. Dieses Projekt redet mit echter Funkhardware und mit
einer Hausautomation, in der ein stiller Fehler teuer sein kann — deshalb sind
die Regeln hier strenger als in einem gewöhnlichen Hobbyprojekt.

Das vollständige Regelwerk steht in **[CLAUDE.md](CLAUDE.md)** und hat Vorrang
vor allem, was hier zusammengefasst ist. Diese Datei nennt nur die Punkte, an
denen Beiträge in der Praxis scheitern.

> 🇬🇧 Contributions in English are welcome — the rules below apply unchanged.
> `CLAUDE.md`, which they summarise, is in German.

---

## Die fünf Regeln, an denen es hängt

**1. `lib/asksinpp/upstream/` wird nie direkt editiert.**
Das ist ein Submodul mit Fremdcode. Jede nötige Änderung an AskSin++ ist ein
nummerierter Patch in `lib/asksinpp/patches/NNN-kurzbeschreibung.patch`, mit
Kommentarkopf, der das *Warum* erklärt. Grund: Die Bibliothek soll auf neue
Upstream-Stände rebasebar bleiben. Was sich als Shim in `port/` lösen lässt,
gehört nach `port/` und **nicht** in einen Patch.

**2. Keine Firmware-API aus dem Gedächtnis.**
Bevor eine `furi_*`, `furi_hal_*`, `storage_*` oder `gui`-Funktion verwendet
wird, ist sie gegen die Symboltabelle des SDK zu belegen:

```bash
grep "furi_hal_subghz" ~/.ufbt-unleashed/current/sdk_headers/f7_sdk/targets/f7/api_symbols.csv
```

Spalte 2: `+` = aus dem FAP aufrufbar, `-` = **nicht** aufrufbar. Ein `-` fällt
nicht beim Kompilieren auf, sondern erst zur Laufzeit auf dem Gerät.

Und auch ein `+` genügt nicht, wenn eine Struktur über die FAP-Grenze geht:
Das Symbol ist dann auflösbar, über das Speicherlayout sagt es nichts.

**3. Registerwerte werden nicht geraten.**
CC1101-Register kommen 1:1 aus AskSin++ (`cc1101.h`, `initVal[]`). Sie werden
nicht „optimiert" und nicht aus einem Flipper-OOK-Preset abgeleitet. Ein
einziges falsches Register bedeutet: die Zentrale sieht das Gerät nie — ohne
jede Fehlermeldung. Register `0x00` darf in der Preset-Tabelle überhaupt nicht
vorkommen (CLAUDE.md §5.3.2a).

**4. „Sollte funktionieren" ist kein Testergebnis.**
Änderungen am Funk-, Speicher- oder Anlernpfad gelten erst als fertig, wenn sie
**gegen eine echte Zentrale** getestet sind, mit Angabe, was beobachtet wurde.
Ungetestete Änderungen sind als ungetestet zu kennzeichnen — das ist völlig in
Ordnung, stilles Behaupten nicht.

**5. Warnungen im eigenen Code sind Fehler.**
`ufbt` muss sauber durchlaufen. Warnungen aus `upstream/` werden über
`Lib(cflags=...)` in `application.fam` gedämpft — **nie** über ein globales
`-w`, das würde die Warnungen in `src/` und `port/` mit abschalten.

---

## Vor dem Pull Request

```bash
./scripts/setup.sh                                  # Submodul + Patches
UFBT_HOME=~/.ufbt-unleashed ufbt                    # muss warnungsfrei sein
UFBT_HOME=~/.ufbt-unleashed ufbt launch             # muss starten und sauber beenden
```

Checkliste:

- [ ] Build läuft warnungsfrei durch (eigener Code).
- [ ] Die App startet auf Hardware und lässt sich beenden — kein Furi-Crash,
      kein hängender Record. `free` in der CLI vor und nach dem Lauf vergleichen.
- [ ] Für jede neu verwendete Firmware-API ist der `+`-Status belegt.
- [ ] Jedes `// TODO(verify)` ist aufgelöst oder im PR-Text als offener Punkt
      benannt.
- [ ] Bei Funkänderungen: Beobachtung an echter Hardware beschrieben.
- [ ] `CLAUDE.md` mitgezogen, wenn sich eine Regel oder der Stand ändert.

Änderungen bitte klein halten und pro Schicht trennen (Shim / Radio / Storage /
Device / UI), damit sie einzeln auf Hardware verifizierbar sind.

---

## Was in Issues gehört — und was nicht

Hilfreich sind: Firmwareversion und API-Version des Geräts, SDK, mit dem gebaut
wurde, Typ der Zentrale, und die Diagnosezeile aus dem Log
(`diag tx=… rx=… crcerr=… busy=… oversz=… gdo0=…`).

**Nicht posten:**

* den Inhalt von `aes_key.txt` oder einen Systemsicherheitsschlüssel,
* `eeprom.bin` (enthält HMID, Seriennummer und Peers deiner Installation),
* ungekürzte Logs mit den HMIDs deiner anderen Homematic-Geräte.

Ein Fund zum echten eQ-3-Werksschlüssel gehört ebenfalls **nicht** in ein
Issue. Dass der Schlüssel unverifiziert ist, ist der bekannte offene Punkt
(README) — gesucht ist die Bestätigung, *ob der Signaturpfad stimmt*, nicht
die Veröffentlichung von Schlüsselmaterial.

---

## Lizenz von Beiträgen

Mit einem Pull Request stimmst du zu, dass dein Beitrag unter
**CC BY-NC-SA 3.0 DE** steht — derselben Lizenz wie das Projekt. Die ist durch
das ShareAlike-Gebot von AskSin++ vorgegeben und nicht frei wählbar; siehe
[LICENSE](LICENSE) und [NOTICE.md](NOTICE.md).
