# D5.1 Test Card — House Census

**Stage:** D5 — Capability applications (Roadmap §D5), app 1 of 4.
**Design basis:** Fork Design v0.1 §6.1 (House Census), §5.1 (tiers).
**Rule:** receive/inventory-first. Transmission experiments are separate tasks, never a side effect of taking census (§6.1).

## Purpose

A local ledger of the household's invisible emitters and objects: what Sub-GHz signals the house contains, which NFC/RFID objects exist, which infrared devices are present — each with timestamps, protocol names, signal metadata, and Jacob-assigned friendly names; exportable in human- and machine-readable forms.

## Versions

- **v0.1 (this card):** census core (record store, persistence, export, naming sidecar) + **Sub-GHz receive collector** (protocol decoders, three listening frequencies).
- **v0.2 (next increments, same stage):** NFC/RFID inventory collector; infrared profile collector. Each lands with its own acceptance run before D5 moves to Fleet Tags.

## Data schema

One record per unique emitter/object, stored as JSON Lines at
`/ext/apps_data/house_census/census.jsonl`:

```json
{"kind":"subghz","protocol":"Princeton","hash":305419896,"freq_hz":433920000,"preset":"AM650","count":3,"first":"2026-10-21 05:12:44","last":"2026-10-21 05:13:01","rssi_dbm":-72,"detail":"Key: 0x…; Btn: …","name":""}
```

- `hash` — FNV-1a over the canonical decode string; the dedup key is `(kind, protocol, hash)`. Known limit: rolling-code protocols vary their decode string per press and may occupy several records until Jacob names them; naming merges them socially, not mechanically, in v0.1.
- `name` — Jacob-assigned friendly name, applied from the sidecar `/ext/apps_data/house_census/names.txt` (`hash=name` per line) at boot; Athena edits the sidecar over RPC, Jacob rules the names.
- Timestamps are device RTC (host-synced via the D4 handshake discipline before a driven session).

## Local screen language

- **Menu:** title `House Census`; unique-record count; session observation count; lines: `OK Listen`, `L/R Freq`, `Down Export`, `Back Exit`.
- **Listening:** frequency + preset; `RX <n>` decodes this session; last protocol + short hash; last RSSI; `OK Stop`.
- Language is inventory language ("records", "observed"), never surveillance language; the screen says what the device heard, nothing more.

## Athena RPC workflow

The Census FAP is a standalone foreground app (like the stock Sub-GHz app). Athena drives it by: session → start by path → GUI stream for the screen → injected keys (OK to listen, Left/Right for frequency, Right to export) → read `census.jsonl` + exports over Storage Read → stop session. No Link v1 binding in v0.1 (the Hub remains the Link endpoint); a driven census session is Tier 0/1 work in an ordinary session.

## Export format

- Machine-readable: `census.jsonl` (the store itself).
- Human-readable: `exports/census.md` — a table (name/protocol/frequency/count/first/last/detail) written on Export and on exit.

## Removal / rollback

Delete `/ext/apps/Tools/house_census.fap` and `/ext/apps_data/house_census/`. No firmware, settings, or stock-app state is touched; the radio is released on exit (worker stopped, device ended, devices deinit).

## Tier classification (every action)

| Action | Tier | Notes |
|---|---|---|
| Sub-GHz listen (receive + decode) | **0 — Observe** | Passive reception only; the CC1101 is never keyed to transmit. |
| Writing census records / exports (own app data) | **1 — Reversible** | Checksummed transfers; deletable without trace. |
| Assigning friendly names (sidecar edit) | **1 — Reversible** | Jacob rules the names. |
| Any Sub-GHz / IR transmission | **Not in Census** | Separate Tier 2 tasks under their own authorization (§6.1). |
| NFC/RFID read, IR learn (v0.2) | 0/Inventory | Reader-field inventory functions, operator-present; classified on their own card increment. |

## Hardware acceptance card (v0.1)

1. App launches by Jacob's hand and by Athena (path start); menu renders; record count matches the store.
2. Listening starts on 433.92 MHz AM650; screen shows frequency, RX count, and stays responsive; OK stops; Back exits; desktop returns; serial healthy afterwards (D4 discipline).
3. A real house emitter decodes (Jacob presses a house remote near the unit): a record appears with correct protocol name, count increments on repeat presses, timestamps are synced, RSSI recorded.
4. Frequency switch (Left/Right) retunes and keeps listening; records carry the right `freq_hz`.
5. Persistence: exit + relaunch → records reload from `census.jsonl` with counts intact.
6. Export: `exports/census.md` exists and matches the store; Athena reads both files over RPC and verifies counts + MD5 where applicable.
7. No transmission: the app contains no TX call path (source-auditable); nothing is emitted at any point in the run.
