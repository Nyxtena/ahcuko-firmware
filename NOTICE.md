# NOTICE — provenance & licensing ledger

This project stands on the work of others. This file records whose, under what
terms, and how this project honors them. It is updated whenever code is adopted,
adapted, or newly derived.

## Upstreams

1. **Flipper Zero official firmware** — © Flipper Devices Inc. and contributors.
   Source: https://github.com/flipperdevices/flipperzero-firmware
   License: **GNU General Public License v3.0 (GPL-3.0)**.
   Role in this project: the base from which ahcuko-firmware will be built.
   Supporting official materials (RPC protocol definitions, uFBT tooling,
   documentation) come from the same organization under their stated licenses.

2. **Unleashed firmware** — © the DarkFlippers team (lead: xMasterX) and
   contributors.
   Source: https://github.com/DarkFlippers/unleashed-firmware
   License: **GNU General Public License v3.0 (GPL-3.0)**.
   Role in this project: **reference material only** — studied to understand the
   community's changes (Sub-GHz policy, feature additions, app ecosystem). If
   any Unleashed code is ever adopted or adapted, it will be recorded here with
   file-level attribution before it lands, and it remains GPL-3.0.

3. **ESP32 Marauder** — © justcallmekoko and contributors, MIT License.
   Role: none in this repository. It was researched for the separate Wi-Fi dev
   board question and is recorded in the project's private papers, not here.

## This project's commitments (GPL-3.0)

- ahcuko-firmware is licensed **GPL-3.0** (see `LICENSE`).
- Copyright and license notices of upstream code are preserved verbatim in
  every file that carries them.
- Modified upstream files will state that they were changed, and when.
- Any distributed build of this firmware is accompanied by its complete
  corresponding source, published in this repository.
- No additional restrictions are placed on recipients' GPL rights.

## Device facts (for context)

Ahcuko is a retail Flipper Zero (hardware rev 12, US region) owned by Jacob.
As first verified on-device 2026-10-20, she runs the official firmware 1.4.3;
her earlier unlocked firmware was, per Jacob, DarkFlippers Unleashed, since
removed by a reformat. Nothing in this project is affiliated with or endorsed
by Flipper Devices Inc. or the DarkFlippers team.
