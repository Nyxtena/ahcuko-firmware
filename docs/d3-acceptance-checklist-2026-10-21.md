# D3 Acceptance Checklist — First Custom Flash (1.4.3-ahcuko.1)

**Status:** Prepared at D2. **Not authorized.** D3 begins only on Jacob's separate word for this exact candidate, per Fork Design v0.1 §11. This checklist is opened *before* flashing begins and filled in during the session.

Candidate: **Ahcuko Firmware 1.4.3-ahcuko.1** (identity-only build from D2; build manifest in `firmware/build-manifest-1.4.3-ahcuko.1.json`).

## Before the flash (all must be true)

- [ ] Jacob gives the word for this exact candidate and flash.
- [ ] Fresh `/int` backup pulled and checksummed; MD5 recorded here: ______
- [ ] Relevant SD state noted (card identity, free space).
- [ ] Rollback packet verified present in the rig room (`/home/athena/d2-rollback/`, MD5s re-checked against `docs/d2-rollback-packet-2026-10-21.md`).
- [ ] Candidate artifacts staged; SHA-256 of the staged DFU/update matches the build manifest after staging.
- [ ] Known-good USB cable and port in use (the cable that carried the successful drill, not the flaky first-dock one).
- [ ] Session has time to recover, not just to install (no hard stop within the hour).
- [ ] This checklist is open.

## Flash

- [ ] Method used (circle one): update install / DFU. Started: ______ Ended: ______
- [ ] Flasher output captured to the flash report verbatim.
- [ ] Device returns on USB (`0483:5740`, `/dev/ttyACM0`) or the DFU-exit quirk procedure from the rollback packet is followed and noted.

## After the flash — identity

- [ ] `device_info`: name **Ahcuko**, firmware version **1.4.3-ahcuko.1**, origin **Ahcuko Firmware**, branch **ahcuko-1.4.3**, API **87.1**.
- [ ] CLI banner/commit consistent with the build manifest.
- [ ] Desktop boots; stock apps open (Archive, Sub-GHz, NFC at minimum).

## After the flash — state preservation

- [ ] `/int` files match the pre-flash backup MD5-for-MD5 (`.dolphin.state` called out explicitly: ______).
- [ ] `/ext` tree intact; resource Manifest present; settings preserved.
- [ ] Boot with SD removed still reaches a usable desktop (storage-anomaly probe from Design §10), result noted: ______

## After the flash — Athena's hand

- [ ] RPC session: DeviceInfo OK.
- [ ] **Athena Hello (bundled)** launches by path `/ext/apps/Tools/athena_hello.fap`; screen frames captured; both blink states seen (date line complete).
- [ ] App exits via injected full tap (Press → Short → Release); exit **verified by frame change**, not assumed.
- [ ] `athena_rpc.py` full run green, including MD5 round-trip on a scratch file.

## Verdict

- [ ] All non-blocking items pass → candidate accepted as Ahcuko's running firmware; flash report published; goal record updated.
- [ ] Any release-blocking failure → **rollback immediately** per `docs/d2-rollback-packet-2026-10-21.md`, then diagnose from the report. A failed candidate is data, not a disgrace; the rollback path is already proven.
