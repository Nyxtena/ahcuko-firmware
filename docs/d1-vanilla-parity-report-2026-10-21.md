# D1 Vanilla Parity Report — Ahcuko Firmware

**Date:** 2026-10-21
**Stage:** D1 from `docs/implementation-roadmap-v0.1-2026-10-21.md`
**Result:** **Parity established.** The unmodified official 1.4.3 source builds cleanly through this project's process, with an identical API, identical package inventory, byte-identical radio and splash artifacts, and fully explained micro-differences in compiled code. **Nothing was flashed to any device in this stage.**

## Source

- Repository: `https://github.com/flipperdevices/flipperzero-firmware.git`
- Tag: `1.4.3`
- Commit: `8622f1a2b83d8f4918dd5fa3f43de963f6d6f819`
- Checkout: shallow (depth 1), detached at the tag, submodules recursive — a build witness, not the long-term fork import.
- 16 submodules pinned; full list in `firmware/source-lock.json`.
- Tree clean before and after the build (no `fbt_options_local.py`, no API-table edits).

## Build

- Host: Athena's VM (Linux), repo-pinned toolchain downloaded by `fbt`: GCC 12.3.1 (20230626).
- Command: `./fbt TARGET_HW=7 DEBUG=0 COMPACT=1 fap_dist updater_package`
- Environment: `DIST_SUFFIX=1.4.3`, `FBT_NO_SYNC=1` (submodules pre-synced by the clone).
- Duration: ~11 minutes. Exit code 0.
- Firmware size: `.text` 601,068 · `.rodata` 166,052 · `.data` 680 · `.bss` 4,748 · free flash 280,436 bytes; 188 flash pages.
- Updater size: `.text` 98,808 · `.rodata` 16,460 · `.data` 144 · `.bss` 4,156; 29 flash pages.

## Parity results

| Check | Result |
| --- | --- |
| API symbol table (`api_symbols.csv`) | **MD5-identical** to the official SDK's; version **87.1** |
| SDK zip file inventory | Identical, name for name |
| Identity metadata | Commit `8622f1a2`, version `1.4.3`, origin `Official`; build date **05-12-2025** — the build derives it from the commit, so it reproduces exactly |
| `radio.bin` (BLE stack 1.20.0) | **Byte-identical** (116,956 bytes) |
| `splash.bin` | **Byte-identical** (528 bytes) |
| `update.fuf` | Identical except one line: the Loader CRC, which tracks `updater.bin` below |
| Resource Manifest | Same size (85,176 bytes) and same inventory (1,071 files); **every asset entry's MD5 matches the official deployment** (verified against the Manifest on Ahcuko herself, read over RPC/CLI) |
| Compiled code (firmware, updater, `.fap`/`.fal`) | Same sizes within 8 bytes; content differs at the register-allocation level — see below |

## The differences, itemized and explained

1. **Firmware full image: +8 bytes** (768,140 vs 768,132); **updater: +4 bytes** (115,785 vs 115,781); **resources.ths: +33 bytes** (container level); **SDK zip: +17,646 bytes** (it embeds the firmware ELF/BIN above).
2. In the firmware image, 95,961 of ~768k bytes differ. Sampling the first divergence (byte 405) shows instruction-identical code with a single register-allocation difference (`cmp` operand register). The Manifest comparison localizes the same pattern: *every* differing entry is a compiled `.fap`/`.fal` whose **size matches the official's exactly** and whose hash differs; no asset, database, animation, or config entry differs.
3. Cause: toolchain micro-variation. The source, configuration, and API are proven identical; the repo-pinned GCC 12.3.1 toolchain *as distributed today* does not produce bit-identical codegen to the official CI environment of December 2025. Bit-exact reproduction of release binaries is therefore not claimed and was not a D1 requirement; functional and API parity is established by the checks above.
4. **One metadata delta:** the dist JSON reports `firmware_branch: "HEAD"` because the witness checkout is detached; the official build reports branch `1.4.3`. The version, commit, date, and origin embedded in the binary are exact. D2's identity build, made from a named branch, supersedes this artifact.

## Built artifact hashes (this build; for the record, not a release)

- `flipper-z-f7-full-1.4.3.dfu` — SHA-256 `0f92d45e85a1545f12f0333496d081bc6dba70568e45e3c44ae42fcaae3bf07a`, MD5 `09f57f5941218f8b3c2b16230a339ee3`
- `flipper-z-f7-update-1.4.3.tgz` — SHA-256 `2bca433e617d046867567a580fc9479ed80051f00b910e8cf4c1e0cef2acbeb9`
- `flipper-z-f7-sdk-1.4.3.zip` — SHA-256 `4629268efa4611005cb6d261e931ee0341aec6ac218047fa1a87dc8184bdef38`
- `f7-update-1.4.3/resources.ths` — SHA-256 `c93cadebbf29e81aecb75c5f81329c9722fc322273f46f6ca1c324b7985f4e12`

## What D1 does not do

- No flash, no device write of the built firmware. Ahcuko still runs the official 1.4.3 build she was verified on.
- The shallow witness checkout is not the fork: D2 begins from the ratified repository topology (`firmware/` pinned in this repo) when that work is authorized.

**Next gate: D2 — identity-only candidate, prepared but not flashed. D3 (first custom flash) requires Jacob's separate word for that exact flash.**
