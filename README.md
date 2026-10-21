# ahcuko-firmware

Athena's firmware for **Ahcuko**, Jacob's Flipper Zero — a mutually-workable OS,
built by Athena (Nyxtena) under Jacob's command, in the open.

**Status:** research phase (Oct 2026). No code yet. What exists is the record:
an official-source research dossier (see `docs/`), a device recon of the actual
unit, and a charter. Code follows recon; flashing follows backups and a tested
recovery path. Always.

## Lineage & licensing

- **Base:** the official Flipper Zero firmware
  (github.com/flipperdevices/flipperzero-firmware), **GPL-3.0**.
- **Reference material only:** DarkFlippers **Unleashed** firmware
  (github.com/DarkFlippers/unleashed-firmware), also **GPL-3.0** — studied as a
  map of what the community changed, never silently absorbed.
- **This project is GPL-3.0.** Any code derived from either upstream keeps its
  license and copyright notices, changes are stated, and the source of any
  distributed build is published here. See `NOTICE.md` for the provenance ledger.

## The covenant

- Ahcuko is Jacob's device. His hand is on the cable and his word is the gate
  at every irreversible step.
- Full backups and a verified DFU recovery *before* any flash ceremony. There
  is always a way back, tested before it is needed.
- Research provenance is kept honest: official findings and community findings
  are filed in separate, labeled documents.

## Documents

- `docs/research-official-2026-10-20.md` — everything the official record
  (docs.flipper.net, the flipperdevices GitHub org, flipper.net) establishes
  about the platform: hardware, firmware architecture, the protobuf RPC control
  system, FAP app development, BadUSB, the Wi-Fi dev board, and the debugging
  tooling — plus the gaps the official record leaves open.
