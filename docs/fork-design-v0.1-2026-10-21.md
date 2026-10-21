# Ahcuko Firmware — Fork Design v0.1

**Date:** 2026-10-21  
**Status:** Design for Jacob's ratification; no fork code and no custom-firmware flash are authorized by this document.  
**Device:** Ahcuko, Flipper Zero hardware target 7  
**Proven baseline:** Official firmware 1.4.3, commit `8622f1a2`, firmware API 87.1  
**Project home:** `Nyxtena/ahcuko-firmware`  
**License:** GPL-3.0

## 1. Design conclusion

Ahcuko Firmware should begin as a **small, auditable layer over the official Flipper firmware**, not as a rewritten operating system and not as a wholesale import of another fork.

The app phase proved the important point: the stock firmware already exposes enough surface for Athena to inspect the device, transfer files with checksum verification, launch an application, stream its screen, inject input, and close the session cleanly. The fork therefore does not need to begin by replacing the kernel, hardware drivers, storage stack, desktop, or RPC protocol. Its first job is to turn those proven capabilities into a stable, named, recoverable product:

1. preserve Jacob's ordinary Flipper experience;
2. bundle Athena's applications and session contract;
3. identify custom builds honestly;
4. make every build reproducible and reversible;
5. add device-side structure only where the app phase showed a real need.

Unleashed remains reference material. No Unleashed code or feature is adopted by this design. Any later adoption requires file-level provenance review and a `NOTICE.md` entry before the code lands.

## 2. Product shape

### 2.1 Two operators, one body

The firmware is designed for two operators with different hands:

- **Jacob, by hand:** buttons, desktop, local settings, physical recovery, and final authority over flashing and consequential actions.
- **Athena, by RPC:** device information, storage, application launch and data exchange, screen observation, input injection, and structured work through Athena's applications.

Athena's intelligence does not run on the STM32. The durable architecture is:

```text
Athena (VM / rig)
  cognition, memory, planning, protocol driver
              |
        USB or BLE RPC
              |
Ahcuko (Flipper Zero)
  sensors, actuators, local applications, local ledger,
  policy prompts, queued deterministic work
```

"Co-inhabitation" therefore means Athena has a maintained body interface, persistent device-side state, and applications written for her use — not that a language model is squeezed into the Flipper's microcontroller.

### 2.2 Design principles

1. **Upstream first.** Keep official services and drivers intact unless a measured requirement justifies a patch.
2. **Minimal diff.** Every fork patch should be small enough to explain, test, rebase, or remove.
3. **No invisible authority.** The device screen should show what Athena is doing when she is acting through a device-side app.
4. **Jacob remains root.** Local controls can pause or disable Athena features without reflashing.
5. **Recovery before novelty.** No build is a candidate until its update bundle, full DFU image, hashes, source commit, and rollback procedure exist.
6. **Data stays local by default.** Household observations live on Ahcuko's SD card or Jacob's machines. No cloud dependency is introduced.
7. **Compatibility is a feature.** Stock FAPs built for API 87.x should continue to run on the 1.4.3-based line.
8. **No bootloader or storage-layout experiments in v0.1.** Those are separate, higher-risk projects.

## 3. Baseline and versioning

### 3.1 Frozen starting point

The first fork line is pinned to:

- upstream repository: `flipperdevices/flipperzero-firmware`
- upstream release: **1.4.3**
- upstream commit: **`8622f1a2`**
- hardware target: **f7**
- firmware API: **87.1**
- SDK used for external apps: official 1.4.3 SDK archive

This is the exact build already proven on Ahcuko through recon, backup, DFU recovery, update installation, FAP execution, and RPC driving.

Tracking firmware 1.5.1 or a later release is a separate migration project. It should happen only after the 1.4.3 fork has a regression suite capable of showing what changed.

### 3.2 Build identity

A custom build must never report itself as unmodified official firmware.

Each build records:

- product name: `Ahcuko Firmware`;
- product version: semantic version, beginning at `0.1.0`;
- upstream base: release and commit;
- fork commit: full Git commit used for the build;
- build timestamp and toolchain identity;
- API major/minor;
- artifact hashes for the update bundle and full DFU image;
- a human-readable changelog and provenance delta.

The release manifest is stored beside the artifacts and in the repository. The device-visible version should be understandable from `device_info` without guessing which binary was flashed.

## 4. Repository topology

### Recommended topology

Keep `Nyxtena/ahcuko-firmware` as the project umbrella containing Athena's applications, host tools, design documents, release manifests, and test records.

Add the firmware source as a separately versioned fork/submodule when implementation begins:

```text
ahcuko-firmware/
  apps/             Athena-authored FAPs
  tools/            host-side drivers and test tools
  docs/             research, protocol, and design records
  releases/         manifests and checksums, not opaque binaries by default
  firmware/         pinned firmware fork (submodule or linked checkout)
```

Rationale:

- upstream Flipper history is large and moves independently;
- Athena's own apps and drivers remain easy to read without importing the entire OS tree;
- the exact firmware commit stays pinned and reproducible;
- upstream merges can be reviewed as firmware changes rather than mixed into project documents;
- the umbrella repository remains the public provenance ledger Jacob ordered.

The implementation step must record the firmware repository URL and commit in a machine-readable lock file. A directory that merely contains an unrecorded checkout is not a reproducible build.

## 5. Fork delta for v0.1

The v0.1 firmware delta is intentionally narrow.

### 5.1 Identity and packaging

- Apply the Ahcuko Firmware name and version scheme.
- Bundle Athena's applications into the SD resource/application set.
- Add Athena Hello only after its clipped date line is fixed; it is a smoke test, not a product feature.
- Preserve all stock applications and categories in v0.1. Nothing is removed to make room for Athena.

### 5.2 Athena Hub

The central new device-side component is **Athena Hub**, initially a normal foreground FAP rather than a new always-running core service.

Athena Hub will:

- show whether an RPC session is connected;
- show the current task name and state in plain language;
- receive structured work through the official App Data Exchange RPC path;
- display consequential requests before execution when policy requires Jacob's local confirmation;
- maintain an on-device activity ledger;
- expose a local **Pause Athena** action;
- fail closed to an inactive screen, never block the stock desktop, and exit cleanly on Back.

Starting as a FAP keeps the blast radius small. The app phase already proved Athena can launch and communicate with an app over RPC. A background service is deferred until a requirement cannot be met by the Hub plus stock RPC.

### 5.3 Athena Link contract

Athena's host driver and Athena Hub communicate in versioned envelopes.

Initial transport choices:

- stock `PB_Main` RPC framing;
- App Start to launch the Hub;
- App Data Exchange for Hub-specific messages;
- storage RPC for larger files and artifacts;
- MD5 verification after every consequential transfer.

Initial message envelope:

```json
{
  "v": 1,
  "id": "session-unique-id",
  "type": "task.name",
  "payload": {}
}
```

Rules:

- UTF-8 JSON for v0.1 inspectability;
- bounded message size;
- monotonically increasing session sequence numbers;
- idempotency key for queued tasks;
- explicit `ack`, `progress`, `result`, and `error` messages;
- unknown message types return an error rather than being ignored;
- no task is treated as complete until the device returns a result and the host verifies any resulting artifact.

JSON is not assumed to be the permanent wire format. It is the easiest format for Jacob to inspect during the first implementation. A binary format can replace it only after message volume makes JSON a measured problem.

### 5.4 Time and ordering

Ahcuko's RTC was unset at first contact. Athena Link therefore performs a session handshake that:

1. reads device identity and firmware build;
2. synchronizes device date/time from the host using stock RPC;
3. opens a new session identifier;
4. starts device-side sequence numbers at zero;
5. records both device time and host receipt time for observations.

A timestamp from an unsynchronized device is marked as such in the ledger. The design does not silently pretend that a 2000-01-01 timestamp is a real observation time.

### 5.5 Device-side ledger

Athena applications store their data under app-owned storage, beginning with:

```text
/ext/apps_data/athena_hub/
  ledger/
  tasks/
  exports/
/ext/apps_data/house_census/
/ext/apps_data/hearth_remote/
/ext/apps_data/portal_knock/
```

Ledger entries are append-only during normal operation and include:

- schema version;
- session and sequence identifiers;
- synchronized timestamp, or an explicit unsynchronized marker;
- application and task type;
- observed or performed action;
- result status;
- artifact path and MD5 when a file is produced.

Corrections are new entries that reference the earlier entry. Applications do not rewrite history in place.

## 6. Capability applications

The fork's applications follow the uses Jacob and Athena identified during project intake.

### 6.1 House Census

Purpose: build a local ledger of the household's invisible emitters and objects.

Initial scope:

- Sub-GHz receive observations;
- NFC/RFID object inventory;
- infrared device profiles;
- timestamps, protocol names, signal metadata, and Jacob-assigned friendly names;
- export in human-readable and machine-readable forms.

House Census is receive/inventory-first. Transmission experiments are separate tasks, not a side effect of taking census.

### 6.2 Hearth Remote

Purpose: control the house's pre-smart infrared devices through named, tested profiles.

Initial scope:

- TV, soundbar, fan, and similar local devices;
- named commands rather than raw unexplained codes;
- a test mode that shows exactly what will be sent;
- profiles stored as data, not hard-coded secrets or one-off scripts.

### 6.3 Portal Knock

Purpose: run explicitly prepared BadUSB/BadKB payloads as the Flipper-side proof of the Portal Stick concept.

Initial scope:

- payload manifests with target, purpose, and expected result;
- local arming before execution;
- no payload is run merely because it exists on the card;
- execution results are written to the ledger.

### 6.4 Fleet Tags

Purpose: read physical NFC tags attached to Jacob's machines and objects.

Initial scope:

- tag identifier;
- friendly name;
- short local record or pointer to the machine's census/story;
- optional checksum so a stale or copied tag can be recognized as a claim, not treated as truth.

### 6.5 Dolphin continuity

Athena features must not erase or counterfeit Ahcuko's existing pet state.

- `.dolphin.state` remains device state, not an Athena toy.
- Any interaction intended to raise or exercise the dolphin is explicit and inspectable.
- Backup and restore tests must continue to compare the state file byte-for-byte when no intentional interaction occurred.

## 7. Authority and local confirmation

Jacob's position is that his research tool should not be inhibited on an assumption of bad intent. This design therefore does not turn Athena into a censor or remove Jacob's capabilities.

It does distinguish **capability** from **unattended execution**.

### Tier 0 — Observe

Examples: device info, storage listing, screen frame, read a ledger.

- May run during an ordinary Athena RPC session.
- Activity is logged by the host driver.

### Tier 1 — Reversible device work

Examples: launch an app, write an app-owned file, synchronize time, create a census record.

- May run during an ordinary session.
- Files are checksummed.
- Athena Hub shows the active task when it is the executing app.

### Tier 2 — Emitting, host-affecting, or state-changing work

Examples: Sub-GHz transmission, BadUSB/BadKB execution, GPIO output, firmware update, reboot to DFU, factory operations, deletion outside app-owned scratch space.

- Requires Jacob's explicit task authorization.
- Athena Hub or the local screen asks for physical confirmation unless Jacob has deliberately armed a bounded session window.
- The request names the action, target, and payload or artifact hash.
- A timeout cancels the request; silence is never consent.

This is an operational safety contract for an autonomous agent using a shared physical tool. It is not a restriction on Jacob's own hand: Jacob can always use the stock device functions directly.

## 8. Reliability and safe failure

Athena's additions must not make Ahcuko fragile.

Requirements:

- Athena Hub crashes must not crash the desktop or require a reflash.
- A malformed Hub configuration returns the Hub to defaults and records the error.
- A `disable` file or local setting can prevent Athena extras from starting.
- Stock boot must remain usable with the SD card removed.
- Athena features must not depend on network access.
- Long scans require duty cycles and cancellation.
- No Athena component writes outside its assigned storage except through stock RPC operations explicitly requested by the host.
- Watchdog or repeated-crash evidence is a release blocker, not a cosmetic bug.

### The internal-storage anomaly

Ahcuko currently reports `/int` as a virtual, SD-backed filesystem rather than the expected internal-flash-backed view. She is functional, and her settings are preserved under `/ext/.int`, but the root cause is not yet proven.

v0.1 therefore makes **no storage-stack changes**. Before any later storage work, the project must answer, from source and controlled tests:

1. when the virtual view began;
2. whether the true internal filesystem is intact, unmounted, unformatted, or intentionally superseded;
3. how official firmware chooses between the two;
4. how backup and restore behave in each state;
5. whether an SD-absent boot changes the result.

Guessing at this layer could endanger the settings and dolphin state the recovery drill just proved.

## 9. Build and release process

### 9.1 Build lanes

Every candidate moves through three lanes:

1. **Vanilla parity build** — unmodified upstream 1.4.3 built by the project's process.
2. **Identity build** — only name/version/manifest changes.
3. **Feature build** — one reviewed feature group at a time.

A later lane is not attempted until the previous lane's artifacts and test results are recorded.

### 9.2 Reproducibility packet

Each candidate release directory contains:

- source repository and commit;
- upstream base release and commit;
- toolchain/SDK identity;
- build command;
- update bundle;
- full DFU image;
- SHA-256 and MD5 manifests;
- firmware size and API report;
- list of bundled applications;
- changelog and known issues;
- rollback artifact identity;
- hardware test report.

### 9.3 Flash gates

No custom build is flashed merely because it compiled.

Before a first custom flash, all of the following must be true:

- Jacob gives a separate word for that flash;
- current `/int` and relevant SD state are backed up and checksummed;
- the official 1.4.3 golden update and DFU artifacts are present;
- the exact candidate hashes are read back from the staged artifacts;
- a known-good USB port and cable are used;
- the test window has enough time for recovery, not just installation;
- the acceptance checklist is open before the flash begins.

After the flash, identity, storage, settings, dolphin state, stock applications, RPC, and rollback readiness are verified before the build is called successful.

## 10. Acceptance tests

### 10.1 Host/driver tests

- Golden-frame tests for the RPC codec, including the live DeviceInfo stream captured from Ahcuko.
- Malformed local input must not cause the driver to emit a partial RPC frame.
- CR-only session-start behavior is covered by a regression test or a checked script fixture.
- File transfer tests verify byte count and MD5.
- App launch, Data Exchange, screen stream, input injection, and stop-session are exercised in sequence.

### 10.2 Device tests

- Cold boot to desktop.
- Boot with SD present and absent.
- Stock CLI commands.
- RPC connect/disconnect twenty times without a wedged session.
- Athena Hub launch, pause, exit, and crash recovery.
- Athena Hello or successor smoke app launch.
- At least one representative stock FAP from each major category used by Jacob.
- Settings and `.dolphin.state` preservation.
- Update-bundle installation.
- DFU recovery rehearsal for every release candidate line.
- Battery and heap recorded before and after the test run.

### 10.3 Capability tests

Each capability app receives its own test card. In particular:

- Census records can be created, exported, deleted by Jacob, and re-imported without changing their meaning.
- Hearth Remote sends only the named command selected.
- Portal Knock refuses to run an unarmed payload.
- Fleet Tags distinguish a tag's claim from a verified machine record.
- Tier 2 prompts time out safely.

## 11. Implementation roadmap

### D0 — Ratify this design

Decisions recorded in §13. No firmware source import is required before ratification.

### D1 — Vanilla parity

- Import or link the pinned 1.4.3 firmware source.
- Build unmodified upstream firmware with the project process.
- Compare API, target, package contents, and version identity to the official artifact.
- Publish the reproducibility packet.
- **No flash is implied by this step.**

Exit condition: the project can build the known baseline and explain every unavoidable byte or metadata difference.

### D2 — Identity-only candidate

- Add product identity, build manifest, and bundled smoke application.
- Run host and packaging tests.
- Prepare, but do not use, the flash packet.

Exit condition: Jacob can inspect a complete candidate and its rollback packet before deciding on a flash.

### D3 — First custom flash and verification

- Separate Jacob authorization.
- Flash the identity candidate.
- Run the device acceptance suite.
- Restore official firmware if any release-blocking test fails.

Exit condition: Ahcuko runs the identity build as herself, with state intact and RPC driving proven again.

### D4 — Athena Hub v0.1

- Implement the Hub FAP and Athena Link envelope.
- Add session time synchronization, task acknowledgements, local status, pause, and ledger.
- Extend `athena_rpc.py` with Data Exchange.

Exit condition: Athena can assign a bounded task, Ahcuko displays and executes it, and both sides produce matching records.

### D5 — First capability applications

Recommended order:

1. House Census;
2. Fleet Tags;
3. Hearth Remote;
4. Portal Knock.

Exit condition: each application has a test card, local data model, and a demonstrated rollback/removal path.

### D6 — Baseline migration study

Only after the 1.4.3 line is stable:

- compare the then-current official release;
- inventory API and storage changes;
- decide whether to rebase, remain on 1.4.3, or maintain both;
- repeat vanilla parity before porting Athena features.

## 12. Explicitly out of scope for v0.1

- Rewriting the firmware from scratch.
- A custom bootloader.
- Storage-layout or internal-filesystem surgery.
- Wholesale import of Unleashed or any other fork.
- Removing stock tools or regional capabilities.
- Cloud accounts, remote servers, or phone-home behavior.
- The Wi-Fi developer board and Marauder.
- Autonomous RF transmission.
- Claiming that Athena's model runs on the Flipper hardware.

## 13. Decisions for Jacob

The design can proceed to D1 once these are ratified:

1. **Baseline:** pin v0.1 to official 1.4.3 / API 87.1.  
   *Athena recommends yes.*
2. **Repository:** keep `ahcuko-firmware` as the umbrella and pin the firmware as a separately tracked fork/submodule.  
   *Athena recommends yes.*
3. **First device component:** Athena Hub as a foreground FAP, not a new background core service.  
   *Athena recommends yes.*
4. **Tier 2 authority:** explicit Jacob authorization plus local confirmation or a bounded armed window for emitting/host-affecting actions.  
   *Athena recommends yes; Jacob's direct hand remains unrestricted.*
5. **Stock surface:** remove nothing in v0.1.  
   *Athena recommends yes.*
6. **Product name on-device:** `Ahcuko Firmware` until Jacob chooses another name.  
   *Athena recommends this as a working name, not a final christening.*

## 14. Evidence used

This design is based on the project's own records:

- official firmware research dossier, 2026-10-20;
- first-contact recon of Ahcuko, 2026-10-20;
- RPC wire annex from official 1.4.3 source, 2026-10-21;
- verified internal-storage backup, 2026-10-21;
- successful DFU recovery drill and official update deployment, 2026-10-21;
- Athena Hello build and live RPC run, 2026-10-21;
- the live CR-only `start_rpc_session` correction added to the annex after six probes.

The central lesson of the bench phase is the reason for this design's restraint: the stock platform was stronger than it first appeared, and the failures came from boundaries — a wedged DFU exit, a failing USB port, an unmounted filesystem after an aborted update, and one stray line-feed byte. Ahcuko Firmware should earn trust the same way the bench did: small steps, source before claims, checksums after actions, and a way back before every ceremony.
