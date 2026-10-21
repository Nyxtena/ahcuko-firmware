# D2 Identity Candidate Report — Ahcuko Firmware 1.4.3-ahcuko.1

**Date:** 2026-10-21
**Stage:** D2 from `docs/implementation-roadmap-v0.1-2026-10-21.md`
**Result:** **A complete, reversible identity candidate exists.** It is built, verified, and packaged with its rollback packet and acceptance checklist. **It has not been flashed.** D3 requires Jacob's separate word for this exact candidate.

## The candidate

- **Fork:** branch `ahcuko-1.4.3` from official tag 1.4.3 (commit `8622f1a2…`), three identity commits, tip `18a77218`, tagged `1.4.3-ahcuko.1`.
- **The entire delta from upstream is 132 patch lines across 3 files** (`firmware/identity-1.4.3-ahcuko.1.patch`): one options file and the smoke app's two files. Nothing else in the OS changes.
- Commits:
  1. `4d2889e6` — FIRMWARE_ORIGIN set to the product identity.
  2. `4ca48e4f` — `athena_hello` v1.1 bundled in `applications_user/` (force-added; upstream gitignores that directory).
  3. `18a77218` — origin corrected to the single word **"Ahcuko"** (below).

## Verified identity (from the built artifacts, not the intentions)

| Field | Value |
| --- | --- |
| Firmware version | **1.4.3-ahcuko.1** (embedded string + `update.fuf` Info) |
| Branch | **ahcuko-1.4.3** (the D1 witness build's "HEAD" wart is gone) |
| Commit | **18a77218** |
| Build date | 21-10-2026 (the identity commits' date — honest, not backdated) |
| Origin string in binary | **Ahcuko** |
| API | **87.1 — `api_symbols.csv` MD5-identical to official** (`eeaf0fc6…`) |
| Radio stack | `radio.bin` **byte-identical** to the official bundle's |
| Resource delta vs. vanilla | **+2 entries only:** the `apps/Tools` directory and `athena_hello.fap` |
| Tree state after build | Clean; no API or source drift |

Artifact hashes and sizes are recorded in `firmware/build-manifest-1.4.3-ahcuko.1.json`. Headline: full DFU 768,457 B, SHA-256 `6a029d9e…`; update bundle SHA-256 `afdaaa9b…`.

## The origin-string correction

The first identity build carried `FIRMWARE_ORIGIN = "Ahcuko Firmware"` in source, but the generated version header read `"Ahcuko"` — the fbt version pipeline passes the value as a single token and silently truncates at the space. Leaving source and artifact in disagreement was not acceptable in a project whose whole point is auditability, so the source now says exactly what the binary carries: **"Ahcuko"** — which also matches the field's one-word convention (Official, Unleashed). The product name everywhere else remains **Ahcuko Firmware**.

## Athena Hello v1.1 — fixed and proven on the real screen

The clipped date is fixed: the last row now alternates between `2026-10-21` (complete, every digit on screen) and `~ for Jacob ~` on the blink tick. Both states were captured from Ahcuko's screen over RPC (`apps/athena_hello/v1.1-frame-date.png`, `v1.1-frame-dedication.png`). The uFBT-built v1.1 is what runs on her card today; the candidate bundles the same source built by fbt (deterministic across both candidate builds, MD5 `3288e5a7…`).

## Host-driver findings (athena_rpc.py, upgraded)

D2's "run packaging and host-driver tests" produced three durable findings, all verified live and now documented in the driver itself:

1. **Input injection must mimic the hardware sequence — Press → Short → Release.** The GUI (`gui.c`, `gui_input`) discards any non-Press/Release event that is not part of a complementary Press…Release sequence. Phase 4's bare injected Short was silently discarded, so **Phase 4's app never exited** — it was still running an hour later, which surfaced tonight as `ERROR_APP_SYSTEM_LOCKED`. The driver now sends full taps and **verifies the exit by frame change** (`app_exited: true`) instead of assuming it.
2. **App Start by name is registry-dependent; by path is not.** The loader resolves bare names through a scanned registry that can be empty; a full path (`/ext/apps/Tools/athena_hello.fap`) starts directly. (Status 15 = unknown app; 17 = another app running — the driver's status table now matches `flipper.proto` exactly.)
3. **Sessions must be closed politely.** A driver process that dies without `stop_session` can leave the device-side RPC session wedged for the next attempt; the CLI (Ctrl+C + a command) breaks the wedge, and a reboot always does.

Full driver run on stock 1.4.3 with v1.1: write OK, MD5 match, start OK, frames captured, tap-exit verified. **Green.**

One cosmetic note: Ahcuko's desktop currently shows the "Firmware update — Success" banner from tonight's earlier resource update. It is the desktop's idle face, not a fault; any physical keypress clears it, and it does not block app launches.

## Rollback packet & D3 checklist

- `docs/d2-rollback-packet-2026-10-21.md` — official DFU + update bundle staged and hash-verified in the VM workspace **and** the rig room (`/home/athena/d2-rollback/`), with both proven recovery routes and explicit rollback triggers.
- `docs/d3-acceptance-checklist-2026-10-21.md` — the pre-flash gates, identity checks, `/int` + dolphin-state preservation checks, and the Athena's-hand tests (including the SD-removed boot probe for the open `/int` question). **Prepared, not authorized.**

## Roadmap D2 exit review

- [x] Athena Hello's clipped date fixed.
- [x] Ahcuko Firmware identity and build manifest added.
- [x] Only the smoke application and required metadata bundled.
- [x] Packaging and host-driver tests run.
- [x] Official 1.4.3 update + DFU rollback artifacts staged.
- [x] Candidate acceptance checklist prepared.

**Next gate: D3 — first custom flash, only on Jacob's separate word for candidate 1.4.3-ahcuko.1.**
