# D4 Report — Athena Hub v0.1 (shipped as v0.1.2)

**Date:** 2026-10-21
**Stage:** D4 — Athena Hub v0.1 (Fork Design v0.1 §5.2/§5.3/§5.4/§5.5; Roadmap §D4)
**Result:** **COMPLETE.** The Hub runs on Ahcuko (Ahcuko Firmware 1.4.3-ahcuko.1), accepts bounded tasks over Athena Link v1, displays and records them, and returns matching results. Full acceptance: **17/17 checks** in `tools/hub_test.py` and **3/3** in `tools/hub_test_tail.py`, all live against the unit.

## What was built

- **Athena Hub FAP** (`apps/athena_hub/`, appid `athena_hub`, category Tools): a normal foreground FAP. Shows link state, time-sync state and device clock, current task, and ledger size; OK toggles a local **Pause** (task.* then fails with a `paused` error; an `event.pause` frame notifies the host); Back exits cleanly. Launched by hand it shows NO SESSION and waits (fails inactive); launched by the host it binds the RPC app channel.
- **Athena Link v1** over stock RPC App Data Exchange (PB_Main field 65): UTF-8 JSON envelopes `{v, id, seq, type, payload}`; replies are `ack` / `result` / `error` carrying `in_reply`; unknown types return `unknown_type`; malformed input returns `bad_envelope`; duplicate ids return the cached final reply without re-executing (idempotency ring, last 8 ids). Inbound size bounded at 896 bytes.
- **Tasks in v0.1:** `hello` (session handshake), `task.ping`, `task.note` (records a note line in the ledger), `task.ledger_status`.
- **Handshake (§5.4):** host reads DeviceInfo → **stock-RPC SetDateTime (Main field 37)** syncs the RTC → App Start by path with StartRequest **args exactly `"RPC"`** (the device-side `rpc_app.c` replaces that literal with the `RPC %08lX` handle the app parses) → AppState STARTED → `hello` envelope → ack reporting `time_synced` from the device RTC year.
- **Ledger (§5.5):** append-only `/ext/apps_data/athena_hub/ledger/ledger.log` (plus `tasks/` and `exports/` directories). Line format: `ts=<device time|UNSYNCED> session=<id> rx=<n> event=<...> <detail>`. Host mirror via Storage Read (Main 9/10); counts verified equal in testing.
- **Driver** (`tools/athena_rpc.py`): added SetDateTime, Storage Read, App Data Exchange send, App Exit, `op_app_start(args)`, stray-frame collection on input taps, and the `HubClient` session class. Two transport fixes found during D4 are included: write-all send with tty backpressure (from D3 staging), and status accounting that tolerates nanopb omitting zero-valued status fields.

## Acceptance (live, 2026-10-21)

Suite (`hub_test.py`): upload + MD5, handshake ack, time synced, ping result, connected screen captured, note recorded, ledger mirror line-exact, duplicate id → cached result + no re-execution, unknown type error, malformed envelope error, pause event + paused-task error, resume event + ping, clean session stop. **17/17.**

Tail (`hub_test_tail.py`, separate cold start after the disconnect): serial alive after disconnect, Hub exited on close (screen differs from the connected frame), desktop stable. **3/3.**

Roadmap exit criterion — *"Athena assigns a bounded task; Ahcuko displays, executes, records, and returns a matching result"* — is met by `task.note` end to end.

## The disconnect wedge — incident and root cause

Four times during D4 testing, stopping an RPC session left the device's serial stack dead: GUI alive and correct (Jacob photographed the Hub in its SESSION CLOSED state), but zero bytes on CLI or RPC until a Left+Back hardware reset. D1–D3 sessions never showed this; the common factor was a session stopped while an RPC-bound app was involved.

**Root cause, located in the firmware source** (`applications/services/rpc/rpc_app.c`, `rpc_system_app_free`): session teardown delivers the SessionClose event to the app and then **spins — `while(rpc_app->callback) furi_delay_tick(1);` — until the app unregisters its callback** via `rpc_system_app_set_callback(rpc, NULL, NULL)`. The stock `rpc_debug_app` unregisters inside its SessionClose handler. Hub v0.1.0/v0.1.1 deferred unregistration to the app exit path, whose `rpc_dead` guard then skipped it entirely — so the teardown thread spun forever and the serial never returned to the CLI.

**Fix (v0.1.2):** the SessionClose handler unregisters first, inside the handler, then marks the session dead and requests exit (three lines, the reference app's own pattern). Verified by the tail run above: after the fix, a fresh session answers DeviceInfo immediately after a disconnect — the first such success of the night.

Version history: **v0.1.0** lingered on a SESSION CLOSED screen after disconnect (by design at the time); **v0.1.1** exited on close but still deferred the unregister (wedge persisted — that hypothesis is falsified by the v0.1.1 run record); **v0.1.2** unregisters in-handler (wedge eliminated). The Hub's post-disconnect behavior is therefore *exit to desktop*, a deliberate deviation from §5.2's "fail closed to an inactive screen" wording, forced by the firmware's teardown contract; the design's intent (never block the stock desktop, never wedge the device) is served better by exiting. Recorded here for ratification at the next design pass.

A related tooling finding: the rig-room `wake.py` helper printed `WOKE` **unconditionally**, without reading any device response — several "successful wakes" during the incident were no evidence at all. It has been rewritten to verify the CLI `device_info` response before reporting success. Lesson recorded: helpers that assert success must check something.

Also found and fixed in passing: RPC App Start requires the args field to be exactly `"RPC"` for the device to inject the app handle (otherwise the app launches in local mode and DataExchange returns status 21, ERROR_APP_NOT_RUNNING) — now documented in `HubClient.connect`.

## Artifacts

- Hub FAP: 9,820 bytes (v0.1.2, MD5 `a3c4066e8ad932f28ce58d613c131fbb`), APPCHK Target 7 / API 87.1, deployed at `/ext/apps/Tools/athena_hub.fap`. Built with uFBT against the pinned 1.4.3 SDK — the Hub ships as an SD-card FAP on the running D3 firmware; no new firmware candidate was required for D4.
- Evidence frames in `apps/athena_hub/`: `d4-hub-connected.png` (Hub live, connected), `d4-desktop-after.png` (desktop after disconnect + exit).
- Tools: `tools/athena_rpc.py` (extended), `tools/hub_test.py`, `tools/hub_test_tail.py`.

## Known limits of v0.1

- Envelope parser is a strict flat-object extractor for Link v1's own messages — adequate for the controlled envelope set, not a general JSON parser.
- Pause is a local control; the host learns of it via the event frame or a `paused` error (by design).
- "Hub crashes" from the roadmap test list is covered in the contained sense (malformed input, duplicates, disconnects all handled without a crash); a deliberate hard-fault test was not run — a faulting FAP is a system-level event on this firmware, and inducing one was judged out of scope for v0.1 acceptance.
- The Hub is not yet bundled into a firmware candidate's resources; distribution as a card FAP is the D4 state. Bundling is a packaging decision for a later stage.
