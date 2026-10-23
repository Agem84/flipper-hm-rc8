Gebaut gegen **Unleashed `unlshd-086`, API 87.6**.

**Auf dem Flipper muss dieselbe Firmware laufen.** Ein FAP, das gegen ein
anderes SDK gebaut wurde, startet trotzdem fehlerfrei und scheitert erst auf
dem Funk — siehe README, Abschnitt „SDK und Geräte-Firmware müssen
zusammenpassen".

**Installation:** Datei nach `/ext/apps/Sub-GHz/` auf die SD-Karte kopieren.

**Stand:** Das Anlernen an einer echten eQ-3-CCU ist auf Hardware verifiziert.
Ob Tastendrücke ankommen, hängt am AES-Schlüssel — der eingebaute Wert ist ein
unverifizierter Beispielwert und zur Laufzeit über
`/ext/apps_data/hm_rc8/aes_key.txt` ersetzbar. Details im README.

---

Diese App ist eine Portierung von [AskSin++](https://github.com/pa-pa/AskSinPP)
(© pa-pa, CC BY-NC-SA 3.0 DE) auf den Flipper Zero. Auch dieses Binär ist ein
abgeleitetes Werk und steht damit unter **CC BY-NC-SA 3.0 DE** —
Namensnennung, nicht kommerziell, Weitergabe unter gleichen Bedingungen.
Siehe `LICENSE`, `LICENSE-HINWEIS.md` und `NOTICE.md`.

Enthalten ist ausserdem AES-Code aus Daniel Ottes **AVR-Crypto-Lib**
(© 2008, GPLv3 or later), über `upstream/aes.cpp`. Dass dieser Vermerk mit dem
Nicht-kommerziell-Gebot kollidiert, ist der offene Punkt aus
`LICENSE-HINWEIS.md` — ohne dessen Klärung sollte dieses Release nicht
veröffentlicht werden.

Homematic und BidCos sind Marken der eQ-3 AG, Flipper Zero ist eine Marke von
Flipper Devices Inc. Dieses Projekt ist inoffiziell und unabhängig.
