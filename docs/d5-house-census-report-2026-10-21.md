# D5.1 Report — House Census v0.1 (shipped as v0.1.1)

**Date:** 2026-10-21
**Stage:** D5 — Capability applications, app 1 of 4 (Fork Design §6.1; Roadmap §D5; test card `docs/d5-house-census-test-card-2026-10-21.md`)
**Result:** **Built, deployed, and verified live except one acceptance item** — a decode from a real house emitter, which needs a remote pressed near the unit (Jacob's hand). The app is on the unit now, ready for that test.

## What was built

**House Census FAP** (`apps/house_census/`, appid `house_census`, Tools): the census core plus the Sub-GHz receive collector.

- **Census core:** unique-emitter records (dedup key: protocol + FNV-1a hash of the canonical decode string), persisted as JSON Lines at `/ext/apps_data/house_census/census.jsonl`, reloaded at launch; friendly names applied from a `names.txt` sidecar (`hash=name`, Jacob rules the names); human-readable export at `exports/census.md`, written on demand (Down key) and on exit.
- **Sub-GHz collector:** the stock receive chain assembled from library parts — environment + keystore + rainbow tables, protocol registry, receiver with the Decodable filter, worker feeding level/duration pairs — on the internal CC1101, OOK650 (AM650) preset, three listening frequencies (433.92 / 315.00 / 915.00 MHz), retunable live. Per observation it records protocol, decode detail, frequency, RSSI, first/last timestamps (device RTC), and a press count.
- **Receive-only, by construction:** the source contains no transmit call path at all (no async-TX, no transmitter, no worker write) — grep-auditable, per §6.1's rule that transmission is never a side effect of taking census.
- **Screens:** menu (frequency, record count, controls) and listening (frequency/preset, session RX count, last protocol + hash). OK starts/stops listening, Left/Right retune, Down exports, Back exits to a healthy desktop.

## Acceptance (test card items)

1. Launch by Athena (path start) + menu render + record count = store — **PASS**.
2. Listening starts on 433.92 AM650, screen live and correct, OK stops, Back exits, desktop returns, serial healthy after every session — **PASS**.
3. Real house emitter decodes into a record with protocol/count/timestamps/RSSI — **PENDING Jacob's hand** (press a house remote near the unit while it listens; no emitter decoded during two unattended dwells totaling ~75 s, which is expected in a quiet RF house at 5 AM).
4. Frequency switch retunes live and keeps listening — **PASS** (433.92 → 915.00 shown live on screen).
5. Persistence across relaunch — **PASS mechanically** (store loads clean; becomes substantive when item 3 lands the first record).
6. Export exists, matches the store, readable over RPC — **PASS** (`census.md`, 136 B, header + table; `census.jsonl` present, 0 records).
7. No transmission anywhere in the run — **PASS** (source audit + the receive chain never keys the radio).

## Findings from the build

- **Two non-exported symbols blocked the first build at load time** (App Start status 16, `ERROR_APP_CANT_START`, app never ran): `strtok` (libc, marked `-` in the API table) and the variable `subghz_device_cc1101_int`. uFBT's APPCHK did not flag either; the device loader rejects the FAP. Fixes: hand-rolled line splitting, and the exported accessor `subghz_devices_get_by_name()`. A full undefined-symbol sweep against `api_symbols.csv` (name **and** status) is now part of the build routine — all 82 imports verified exported.
- **`subghz_devices_begin()` returns false for the internal radio by design** (its interconnect has `begin == NULL`). v0.1 treated that as fatal and the radio never started; v0.1.1 proceeds like the stock wrapper does. Only a NULL device pointer is fatal now.
- **RPC-injected Left/Right taps step twice** (433 →R→ 915 →L→ 433, symmetric), while OK/Back/Down fire once. The app handler is one-step-per-Press by construction, so the duplication is in the injection path for those keys; the 3-frequency cycle still visits every value. Flagged for confirmation under Jacob's physical hand (hardware input comes from the real input service, where the stock apps behave normally).
- The FAP device chain (devices registry → preset → async RX → worker → receiver decoders) is fully usable from an external app on API 87.1 once the two symbol traps are avoided — the recipe is the stock `subghz_txrx.c` assembly, re-derived from source.

## Artifacts

- FAP v0.1.1: 12,244 B (MD5 `2a86d2b780ebf875edb974f07b65ee47`), APPCHK Target 7 / API 87.1, deployed at `/ext/apps/Tools/house_census.fap`.
- Evidence frames in `apps/house_census/`: `d5-census-menu.png`, `d5-census-listening.png`.
- Next increments on the same test card: v0.2 collectors for NFC/RFID inventory and infrared profiles; then D5.2 Fleet Tags.
