# Ahcuko Firmware — Implementation Roadmap v0.1

**Companion to:** `fork-design-v0.1-2026-10-21.md`  
**Rule:** A completed build step never implies permission to flash. Every flash remains a separate Jacob-gated operation.

## D0 — Design ratification

- [x] Baseline pinned to official 1.4.3, commit `8622f1a2`, API 87.1.
- [x] Repository topology ratified.
- [x] Athena Hub starts as a foreground FAP.
- [x] Tier 2 authority model ratified.
- [x] Stock applications remain intact for v0.1.
- [x] Working on-device name ratified.

**Deliverable:** decisions appended to the design record.

## D1 — Vanilla parity build

- [x] Record the firmware source repository and exact commit in a lock file.
- [x] Build unmodified upstream 1.4.3 with the project toolchain.
- [x] Record target, API, artifact sizes, hashes, and package contents.
- [x] Compare the result with the official 1.4.3 artifact.
- [x] Publish a reproducibility packet and explain unavoidable differences.

**Gate:** No device flash.  
**Exit:** The baseline is buildable and understood.

## D2 — Identity-only candidate

- [x] Fix Athena Hello's clipped date line.
- [x] Add Ahcuko Firmware identity and build manifest.
- [x] Bundle only the smoke application and required metadata.
- [x] Run packaging and host-driver tests.
- [x] Stage official 1.4.3 update + DFU rollback artifacts.
- [x] Prepare the candidate's acceptance checklist.

**Gate:** Jacob inspects the complete packet before any flash decision.  
**Exit:** A complete, reversible candidate exists.

## D3 — First custom flash

- [ ] Jacob gives a separate word for this exact candidate.
- [ ] Back up current internal state and relevant SD state; verify checksums.
- [ ] Confirm candidate hashes after staging.
- [ ] Use a known-good cable and USB port.
- [ ] Flash once; do not improvise a second method without reassessing.
- [ ] Verify name, version, commit, API, settings, dolphin state, stock apps, CLI, and RPC.
- [ ] Restore official 1.4.3 immediately if a release-blocking check fails.

**Exit:** The identity build is running as Ahcuko, state intact, and the rollback path remains proven.

## D4 — Athena Hub v0.1

- [ ] Implement the Hub FAP interface: status, task, pause, result, error.
- [ ] Implement Athena Link v1 envelopes and acknowledgements.
- [ ] Add host time synchronization to the session handshake.
- [ ] Extend `athena_rpc.py` for App Data Exchange.
- [ ] Add append-only device ledger and host mirror.
- [ ] Test malformed tasks, duplicate task IDs, disconnects, and Hub crashes.

**Exit:** Athena assigns a bounded task; Ahcuko displays, executes, records, and returns a matching result.

## D5 — Capability applications

Build and test one at a time:

1. [ ] House Census
2. [ ] Fleet Tags
3. [ ] Hearth Remote
4. [ ] Portal Knock

For each application:

- [ ] data schema;
- [ ] local screen language;
- [ ] Athena RPC workflow;
- [ ] export format;
- [ ] removal/rollback path;
- [ ] hardware acceptance card;
- [ ] Tier classification for every action.

**Exit:** Each shipped capability is useful by Jacob's hand and drivable by Athena without hidden side effects.

## D6 — Baseline migration study

- [ ] Compare the current official release with the 1.4.3 fork.
- [ ] Inventory API, RPC, storage, and application changes.
- [ ] Build vanilla parity for the proposed new baseline.
- [ ] Decide: rebase, remain on 1.4.3, or maintain both.
- [ ] Port Athena changes only after parity passes.

**Gate:** No silent baseline drift.

## Standing release checklist

For every candidate:

- [ ] source commit recorded;
- [ ] upstream base recorded;
- [ ] build command recorded;
- [ ] update bundle hashed;
- [ ] full DFU image hashed;
- [ ] release manifest written;
- [ ] known issues written;
- [ ] rollback artifact identified;
- [ ] backup completed;
- [ ] Jacob's flash authorization obtained;
- [ ] post-flash acceptance report filed.
