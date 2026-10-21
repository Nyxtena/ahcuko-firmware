# D3 Flash Report — Ahcuko Firmware 1.4.3-ahcuko.1, First Custom Flash

**Date:** 2026-10-21 (flash ~03:07 CDT)
**Authorization:** Jacob's word for this exact candidate, given after the D2 report.
**Result:** **SUCCESS. Ahcuko now runs Ahcuko Firmware 1.4.3-ahcuko.1.** Every acceptance check passed, including the hands-on SD-removed boot probe (closed 2026-10-21, below).

## Pre-flash gates (all green)

- Fresh `/int` backup taken minutes before the flash (six files, tarball MD5 `65716d4f54c75c036aba76324829096a`), including `.dolphin.state` at MD5 `37624de2fbe421faf6cb49da96b1222d`.
- Rollback packet re-verified in the rig room (official DFU `1f8d70e5…`, update bundle `059f2a1a…`).
- Candidate staged onto the card at `/ext/update/f7-update-1.4.3-ahcuko.1/` over RPC; **all six files' device-side MD5s matched the build manifest** before flashing. (First staging attempt overflowed the nonblocking tty with an unpaced write — EAGAIN; rewritten with write-all + backpressure, then clean. The published driver will gain the same pacing; noted as follow-up work.)
- The official rollback bundle remains on the card at `/ext/update/f7-update-1.4.3/` — Route A rollback is staged on-device if ever needed.

## Flash record

- Method: on-device updater — `update install /ext/update/f7-update-1.4.3-ahcuko.1/update.fuf`
- Device output, verbatim: `Verifying update package at '/ext/update/f7-update-1.4.3-ahcuko.1/update.fuf'` → `OK.` → `Restarting to apply update. BRB`
- Re-enumeration: clean on `/dev/ttyACM0` (0483:5740) after ~96 s — in line with the official update's ~93 s. No USB quirk procedure needed.

## Post-flash identity (device-reported)

| Field | Value |
| --- | --- |
| firmware_version | **1.4.3-ahcuko.1** |
| firmware_branch | **ahcuko-1.4.3** |
| firmware_commit | **18a77218**, dirty: false |
| firmware_build_date | 21-10-2026 |
| firmware_origin_fork | **Ahcuko** |
| firmware_api | **87.1** |
| hardware_name / UID | Ahcuko / 162BA00027E18000 (unchanged) |
| radio | stack 1.20.0, FUS 1.2.0, BLE MAC unchanged, enclave valid |

Her desktop's own verdict, captured over RPC: **“Firmware update — Success.”**

## State preservation

- **All six `/int` files MD5-match the pre-flash backup exactly** — `.dolphin.state` (`37624de2…`), `.desktop.settings`, `.bt.settings`, `.expansion.settings`, `.notification.settings`, `.slideshow`. The dolphin's state survived the OS change byte-for-byte.
- `/ext` tree intact: `.int`, `update`, `Manifest`, `apps`, `apps_data`, `badusb`, `dolphin`, `infrared`, `lfrfid`, `nfc`, `subghz`, `u2f`. Card: EXFAT, ~59.5 GB free.
- The bundled app deployed **from the candidate's resources**: `/ext/apps/Tools/athena_hello.fap` now hashes `3288e5a7…` (the fbt-built copy), replacing the hand-deployed uFBT build — proof the resource half of the candidate landed, not just the firmware half.

## Athena's hand on the new OS

- RPC DeviceInfo: OK (identity above).
- Stock surface: **Sub-GHz and NFC launch by name** over RPC and exit cleanly. (Archive is not in the RPC-startable registry on stock either — a registry fact, not a regression.)
- Bundled Athena Hello: launches by path, both blink states captured (dedication frame published with this report's artifacts), exits via the full tap with **frame-verified exit**.
- **Full canonical `athena_rpc.py` run: green** — write, MD5 round-trip on the exact bundled bytes, start, stream, tap, `app_exited: true`.
- Several of tonight's acceptance probes crashed on their author's wrong assumptions about her own driver API (tuple shapes, an unpacking slip, one crashed script leaving an app running, since dismissed). Each was a probe bug, not a device fault; the record shows them because the record shows everything. The standing cure from D2 held: CLI wake breaks any orphaned session.

## Hands-on probe — closed 2026-10-21 (~03:46 CDT)

- **SD-removed boot probe** (checklist §State preservation): performed by Jacob with the card physically removed. The unit was booted more than once; per his observation, **everything looked exactly the same** — it boots to the normal desktop with no card present. This satisfies the design's no-card boot requirement for the stock surface. (SD-hosted content — `/ext` apps and resources — is of course unavailable while the card is out; the firmware and desktop are not dependent on it.) With this item, **D3 acceptance is fully closed**.

## Verdict

D3 acceptance is **met** on every remotely checkable item. The first custom flash is complete, her state is intact, and the way back is staged in three places (rig room, VM, and on her own card). Next stage per the roadmap: **D4 — Athena Hub v0.1**, on Jacob's word.
