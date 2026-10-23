# Lizenzhinweis

[`LICENSE`](LICENSE) enthält den rechtsverbindlichen Lizenztext im Wortlaut und
sonst nichts. Diese Datei erklärt, was darunter fällt, was nicht, und warum
die Lizenz so und nicht anders gewählt ist.

---

## Rechtevermerk

```
Homematic BidCos Remote (HM-RC-8) für Flipper Zero
Copyright (c) 2026 Agem

Lizenziert unter Creative Commons
Namensnennung – Nicht kommerziell – Weitergabe unter gleichen Bedingungen
3.0 Deutschland (CC BY-NC-SA 3.0 DE)

Lizenzurkunde:  https://creativecommons.org/licenses/by-nc-sa/3.0/de/
Lizenztext:     https://creativecommons.org/licenses/by-nc-sa/3.0/de/legalcode
```

Der Text in `LICENSE` ist der deutsche Legal Code, wortgetreu von
creativecommons.org übernommen. Die DE-Portierung ist auf Deutsch
abgefasst — eine englische Fassung dieses Lizenzvertrags gibt es nicht.

---

## Warum diese Lizenz und keine andere

Dieses Projekt ist eine Portierung von
[AskSin++](https://github.com/pa-pa/AskSinPP) auf den Flipper Zero. Der Code in
`lib/asksinpp/port/` ist gegen die Klassen und Templates von AskSin++
geschrieben und wird mit dessen Quellen in ein gemeinsames Binär übersetzt —
er ist damit ein abgeleitetes Werk.

AskSin++ steht unter CC BY-NC-SA 3.0 DE; der Vermerk steht im Kopf **jeder**
Upstream-Datei (nachgeprüft: einheitlich über alle `.h`/`.cpp`, siehe
[NOTICE.md](NOTICE.md)). Abschnitt 4.b des Lizenzvertrags verlangt, dass
Abwandlungen nur unter derselben oder einer gleichwertigen Lizenz weitergegeben
werden. MIT, Apache-2.0 und GPL sind damit ausgeschlossen; CC BY-NC-SA und GPL
sind zudem untereinander nicht kompatibel.

Praktische Folgen, kurz:

| | |
|---|---|
| **Namensnennung** | Dieses Projekt **und** AskSin++ (pa-pa) sind bei Weitergabe oder Bearbeitung zu nennen, inklusive Lizenzangabe und Hinweis auf Änderungen — siehe [NOTICE.md](NOTICE.md). |
| **Nicht kommerziell** | Keine Nutzung, die vorrangig auf geschäftlichen Vorteil oder eine Vergütung gerichtet ist. Das betrifft auch den Verkauf vorgebauter `.fap`-Dateien. |
| **Weitergabe unter gleichen Bedingungen** | Bearbeitungen und Forks stehen wieder unter CC BY-NC-SA 3.0 DE. |

Zur Form der Weitergabe: Abschnitt 4.a verlangt, „stets eine Kopie dieser
Lizenz oder deren vollständige Internetadresse in Form des URI" beizufügen.
Eine Verlinkung hätte also genügt — der vollständige Text liegt hier
trotzdem bei, weil er damit unabhängig von der Erreichbarkeit einer Website
ist.

---

## Was diese Lizenz nicht abdeckt

### AskSin++ selbst

Der Upstream-Code liegt **nicht** in diesem Repository, sondern wird als
Git-Submodul (`lib/asksinpp/upstream`) von
<https://github.com/pa-pa/AskSinPP> geholt. Dort gelten die Bedingungen des
Urhebers. Siehe [NOTICE.md](NOTICE.md).

### Der AES-Teil von AskSin++ — offener Punkt

`upstream/aes.h` und `upstream/aes.cpp` tragen **zwei** Vermerke: den
CC-BY-NC-SA-Vermerk von AskSin++ **und** „adoption of Daniel Otte's cool
AVR-Crypto-Lib, Copyright (C) 2008, GPLv3 or later". Diese Dateien sind die
einzigen beiden im gesamten Upstream mit einem GPL-Vermerk, und `aes.cpp` ist
Teil des Builds (`USE_AES`, siehe `application.fam`).

**Für dieses Repository ist das folgenlos** — es enthält keine der beiden
Dateien, weder im Original noch abgewandelt. Wer aus dem Quellcode selbst
baut, stellt ein Binär für sich her; das ist keine Weitergabe.

**Für die Weitergabe eines gebauten `.fap` ist es nicht folgenlos.** Darin
steckt dann GPLv3-Code neben CC-BY-NC-SA-Code, und die beiden Lizenzen sind
nicht miteinander vereinbar: Das Nicht-kommerziell-Gebot ist eine
Nutzungsbeschränkung, die die GPL ihren Empfängern nicht auferlegen darf.
Deshalb liegt hier **kein vorgebautes Binär** bei, und es ist noch kein
Release veröffentlicht.

Das ist eine Feststellung, keine Rechtsauskunft — und es ist eine Frage an den
Upstream, nicht an dieses Projekt: Welche Lizenz für `aes.*` gilt, kann nur
dessen Urheber klären.

### Flipper-Zero-Firmware und SDK

Werden hier weder mitgeliefert noch verändert. Es wird ausschließlich gegen
die öffentliche FAP-Schnittstelle gebaut; Header und Symboltabelle kommen zur
Bauzeit aus dem von `ufbt` geladenen SDK-Archiv.

### Marken

„Homematic" und „BidCos" sind Marken der eQ-3 AG, „Flipper Zero" eine Marke
von Flipper Devices Inc. Dieses Projekt ist inoffiziell, unabhängig und von
keinem der beiden Unternehmen autorisiert.

---

## Keine Gewähr

Gewährleistung und Haftung regeln die Abschnitte 5 und 6 des Lizenzvertrags.
Der Betrieb von Funksendern unterliegt zusätzlich nationalem Recht — siehe
[README.md](README.md), Abschnitt „Recht und Funkbetrieb".
