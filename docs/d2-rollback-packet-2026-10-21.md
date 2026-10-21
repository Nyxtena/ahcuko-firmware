# D2 Rollback Packet — Ahcuko Firmware

**Purpose:** everything needed to return Ahcuko to official firmware 1.4.3 from any D3 candidate state, staged *before* the candidate is flashed. Both routes below were proven live on this device on 2026-10-21 (resource update install, and the full DFU recovery drill).

## Staged artifacts

Staged in two places, hash-verified in both:

- Athena VM: `hidden_files/d2/rollback/` (goal workspace)
- Rig room: `/home/athena/d2-rollback/` (the machine the flash runs from)

| Artifact | Size | MD5 | SHA-256 |
| --- | --- | --- | --- |
| `flipper-z-f7-full-1.4.3.dfu` (official full DFU, commit 8622f1a2) | 768,441 | `1f8d70e5a35d6b9a1f48d25a0291a304` | `375eb4a1c0a55ef3d0948eb93476b6601ab83f4dc0c6f6e33daf506495b2f00d` |
| `flipper-z-f7-update-1.4.3.tgz` (official update bundle) | 2,195,565 | `059f2a1a4cb05caccfe0abf68a620ae9` | `6cac0175c543c409f81e113ae21264dda74b5951c67805860b50ab3a89056f1f` |

Device state backups:

- The original `/int` backup from the bench phase is preserved in the goal workspace (`flip-backup-int-2026-10-21.tgz`, MD5 `c7ca9b87a75810300d3664552d4c2ca1`).
- **D3 takes a fresh `/int` backup immediately before flashing** and records its MD5 in the flash report; this packet's copy is a format example, not the restore source for D3.

## Route A — update install (preferred; device boots and CLI/RPC answers)

1. Stage the official update folder onto the SD card (`update/f7-update-1.4.3/`, all six files MD5-verified after copy — the rig polkit grant route proven 2026-10-21, with Jacob's verbal verification for the mount).
2. Return the card, redock, and run `update install /ext/update/f7-update-1.4.3/update.fuf` from the CLI.
3. Wait for the device to return (~90 s), then verify identity (official 1.4.3, commit 8622f1a2, origin Official, API 87.1) and the `/int` file MD5s against the pre-flash backup.

## Route B — DFU full flash (device unresponsive or Route A fails)

1. From the rig room: CLI `power reboot2dfu` (or Left+Back+release-Back hardware entry).
2. Confirm DFU enumeration (`0483:df11`).
3. `dfu-util -a 0 -s 0x08000000 -D flipper-z-f7-full-1.4.3.dfu` (dfu-util 0.11, installed on the rig).
4. **Known quirk from the proven drill:** the DFU *exit* may wedge (detach failures, USB enumeration errors on the original port). Remedy, in order: unplug/replug on a different port; if the screen still shows DFU mode, hold **Left + Back** for a hardware reset — she boots the flashed image. The write itself was clean in the drill; the wedge is in the exit path, not the flash.
5. Re-provision resources if the card is fresh (Route A against the official bundle), then verify identity and `/int` MD5s as above.

## Rollback triggers (any one is enough)

- Any D3 acceptance-checklist item marked release-blocking fails.
- Device identity, storage, or dolphin state cannot be verified after the candidate flash.
- Athena Hub/extras (later stages) misbehave in a way a reboot does not clear.
- Jacob says so, for any reason or none.
