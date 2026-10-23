# Homematic RC-8 for Flipper Zero

[![build](https://github.com/Agem84/flipper-hm-rc8/actions/workflows/build.yml/badge.svg)](https://github.com/Agem84/flipper-hm-rc8/actions/workflows/build.yml)
[![License: CC BY-NC-SA 3.0 DE](https://img.shields.io/badge/License-CC%20BY--NC--SA%203.0%20DE-lightgrey.svg)](LICENSE)

A Flipper Zero application (FAP) that uses the **built-in CC1101** to act as a
**Homematic BidCos remote control**. It pairs through the regular teach-in mode
of a CCU / RaspberryMatic central unit and then sends key presses as BidCos
telegrams — eight channels, short and long press.

The protocol work is done by **[AskSin++](https://github.com/pa-pa/AskSinPP)**
by *pa-pa*, ported to the Flipper. This repository provides the port around it:
radio layer, EEPROM replacement, Arduino replacement and the user interface.
See [NOTICE.md](NOTICE.md) for attribution.

> 🇩🇪 **The German documents are the authoritative ones** — Homematic is largely
> a German-speaking ecosystem, and all design rationale is written in German:
> [README.md](README.md) for usage, [CLAUDE.md](CLAUDE.md) for the engineering
> rules and the reasoning behind every decision. This page is a summary.

---

## Status

| | |
|---|---|
| **Pairing with a real eQ-3 CCU** | ✅ verified on hardware — the central runs its configuration sequence across all eight channels, the device shows up with eight channels in the web UI, and pairing survives a restart |
| **Key presses (short / long)** | ⚠️ depends on the AES key, see below |
| **AES factory key** | ❌ **unverified** — the built-in value is an example key, not the real eQ-3 factory key |

The central activates `AES_ACTIVE` on every channel during teach-in and then
answers every key press with an AES challenge instead of an ACK. Without a
matching key the key press does not arrive. The `HM_DEF_KEY` compiled in is the
example value from the AskSin++ documentation; the real eQ-3 factory key is
identical across all HM devices but is not published anywhere — and it was
deliberately **not guessed** here. The key can be replaced at runtime without
rebuilding, via `/ext/apps_data/hm_rc8/aes_key.txt`.

---

## Requirements

| | |
|---|---|
| Device | Flipper Zero (target `f7`), built-in CC1101 — no external radio module needed |
| Firmware on the device | **Unleashed `unlshd-086`**, API **87.6** |
| Build tool | [`ufbt`](https://pypi.org/project/ufbt/) |
| Central unit | CCU2 / CCU3 or RaspberryMatic, reachable on 868.3 MHz |
| To build | `git` (the upstream library is a submodule) |

**The SDK must match the firmware on the device.** A FAP takes structure sizes
from the SDK headers; built against a different layout it writes values to the
wrong offsets. This fails **silently** — the build is green and only the radio
side misbehaves. Keep device firmware and build SDK identical.

---

## Build

```bash
git clone --recurse-submodules https://github.com/Agem84/flipper-hm-rc8.git
cd flipper-hm-rc8
./scripts/setup.sh          # or: .\scripts\setup.ps1 on Windows
```

`setup` fetches the submodule and applies the patches in
`lib/asksinpp/patches/`. **The build fails without this step.** Running it
again is safe — already applied patches are detected and skipped.

```bash
UFBT_HOME=~/.ufbt-unleashed ufbt update --hw-target=f7 \
    --url=https://unleashedflip.com/fw/unlshd-086/flipper-z-f7-sdk-unlshd-086.zip
UFBT_HOME=~/.ufbt-unleashed ufbt            # build
UFBT_HOME=~/.ufbt-unleashed ufbt launch     # deploy and run
```

The pinned `--url` gives exactly the SDK this project is verified against; CI
uses the same one. `ufbt update --index-url=https://up.unleashedflip.com/directory.json`
would give the newest Unleashed release instead.

There is **no download** — building it yourself is the only way. The reason is
a licence collision in the library's AES part, not convenience; see
[Licence](#licence). The finished `.fap` ends up in `dist/` and goes to
`/ext/apps/Sub-GHz/` on the SD card.

---

## Controls

Eight channels are shown as a 4×2 grid, together with pairing state, the
acknowledgement of the last transmission and the receive level.

| Key | Action |
|---|---|
| D-pad | select channel 1–8 |
| **OK** short | *short* key press on the selected channel |
| **OK** long | *long* key press — start, repeats while held, end on release |
| **BACK** short | quit the app (state is saved) |
| **BACK** long | **start teach-in** |

The status line reports `ACK`, `NAK`, `T/O` (timeout) or `RGN` (transmission
blocked by the firmware's region check).

To pair: start teach-in on the central, then press **BACK long**. The device
appears as the generic type **HM-RC-X** with eight KEY channels. Its HMID and
serial number are derived from the Flipper's STM32 UID and persisted, so a lost
state file does not create a duplicate device, and two Flippers never collide.

---

## Legal and radio operation

* 868.3 MHz is an SRD band with a **duty cycle limit** (typically 1 % for
  868.0–868.6 MHz in Germany). The app deliberately has **no continuous
  transmission or flooding function**; every transmission follows a user action.
* The app emulates a device **in your own** Homematic network. There is no
  scanning of foreign HMIDs, no rolling-code attack and no takeover of devices
  paired elsewhere.
* The firmware's region lock is **respected, not bypassed**.
* Operating radio transmitters is subject to national law; that responsibility
  is yours.

---

## Contributing

Issues and pull requests are welcome — please read
[CONTRIBUTING.md](CONTRIBUTING.md) first. In short: `upstream/` is never edited
directly, firmware APIs are proven against `api_symbols.csv` rather than quoted
from memory, and "should work" is not a test result.

Particularly wanted: a **verified** hardware test of the key presses against a
CCU — and with it the answer to whether the key path is correct.

---

## Licence

**CC BY-NC-SA 3.0 DE** — attribution, non-commercial, share-alike.
[`LICENSE`](LICENSE) holds the verbatim legal text; note that the German port
of this licence exists in German only, there is no official English wording of
it. What the licence covers and what it does not is set out in
[LICENSE-HINWEIS.md](LICENSE-HINWEIS.md) (German).

The licence was **not freely chosen**: the port code is a derivative work of
AskSin++, whose share-alike term dictates it. MIT, Apache-2.0 and GPL are
therefore ruled out.

> ⚠️ **There is deliberately no pre-built `.fap`.** `upstream/aes.cpp` is part
> of the build and carries a second notice besides the CC one: *GPLv3 or later*
> (Daniel Otte's AVR-Crypto-Lib). A built binary would combine GPLv3 with a
> non-commercial restriction, and the two are not compatible. Building from
> source for your own use is unaffected.

Thanks to **pa-pa** and the AskSin++ contributors — without that library this
project would not exist.

Homematic and BidCos are trademarks of eQ-3 AG; Flipper Zero is a trademark of
Flipper Devices Inc. This project is unofficial and independent.
