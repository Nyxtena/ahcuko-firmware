# Amendment A1 — Mascot Replacement + Flash Report (2026-10-21)

Candidate: **Ahcuko Firmware 1.4.3-ahcuko.2** — branch `ahcuko-a1`,
commit `fc3031f7`, tag `1.4.3-ahcuko.2`, built with
`./fbt TARGET_HW=7 DEBUG=0 COMPACT=1 DIST_SUFFIX=1.4.3-ahcuko.2 fap_dist updater_package`.

Artifacts:
- Full DFU: 767,609 B, SHA-256 `54cf86f45e4b38c7b1546eb829aea497b97484a84902f64e2fa1287cb3ee9240`
- Update tgz: SHA-256 `9178a64e846b91e13b4bafb438b1b68fbac3b2d3300ec7043e2a3325f4d53501`
- `api_symbols.csv` MD5 `eeaf0fc6b14112958c83c72b1b18ab7b` — identical to the
  official 1.4.3 baseline (API 87.1 parity holds; A1 changes no API).

## What changed (Jacob's rulings, design pass 2026-10-21)

- **Desktop idle:** the stock 32 dolphin packs replaced by a curated set of
  13 Athena packs (L1 BlowKiss/Sleep/Stretch/Read/HairPlay/Pout,
  L2 Laptop/Dance/Solder, L3 HairFlip/EdgeSit/WakeUp/TailChase), each with
  bubble lines in Athena's voice.
- **Internal:** BadBattery, NoSd, Tv replaced with Athena scenes.
- **Blocking:** NoDb, SdBad, SdOk, Url, NewMail replaced.
- **Passport:** 9 Athena portraits (happy/okay/bad x levels 1-3; star
  hairpin at L2, cheek heart at L3).
- **Art route:** poses generated as bold line art from Athena's canonical
  reference portrait, threshold-converted. (Code-drawn sprites and photo
  conversions were tried and rejected on the record.)
- **XP:** values stock; deeds now also fire from Athena's own apps
  (Scanner v0.1.1: stored scan = NfcReadSuccess, doubled in a live Link
  session; Hub v0.1.3: task.* = PluginStart; Census v0.1.2: new unique
  decode = SubGhzReceiverInfo), inside the stock 20 XP/app/day caps.

## Build trap found

The fbt resources staging (`build/f7-firmware-C/assets/compiled` and
`.../resources`) is **cumulative across builds**: deleted source packs
persist in the packaged resources until both stagings are purged. The
first ahcuko.2 package silently contained all 32 stock packs alongside
the new 13. Caught by listing the resources Manifest; fixed by purging
both stagings and repacking. Verified final Manifest: only the 13
Athena packs + manifest.txt.

## Flash + verification (2026-10-21 ~07:52-08:02 CDT, Jacob: "proceed")

- Pre-flash `/int` baseline captured over RPC (5 files, copies in the
  project packet); pre-A1 `.dolphin.state` archived beforehand
  (MD5 `9ae535b6294d7efe2a9b380af702a11d`).
- Update staged to `/ext/update/f7-update-1.4.3-ahcuko.2/` over RPC;
  all six files MD5-verified on-device before install.
- `update install` -> updater verified the package, device rebooted,
  "Firmware update: Success".
- Post-flash DeviceInfo: version **1.4.3-ahcuko.2**, branch ahcuko-a1,
  commit fc3031f7, origin **Ahcuko**, API 87.1, radio alive.
- `/int` parity: **all five files MD5-match the pre-flash baseline.**
- `/ext/dolphin` listing: only the 13 Athena packs + manifest.txt.
- **XP reset ceremony:** `/int/.dolphin.state` removed (archived copy
  retained), device rebooted; the service wrote a fresh state file —
  40 bytes, data zeroed end to end (icounter 0, Level 1).
- Desktop captured over RPC: Athena's HairPlay animation running with
  her bubble line ("You like the twintails. Admit it.").

## Open

- Joint-use deed proof: one staged Scanner scan of the Tangem should
  land +6 XP (NfcReadSuccess 3, doubled live) — Passport is the readout.
- Passport "okay" mood refinement (mouth lost in conversion) — v2 pass.
- Updater's own success screen still shows a dolphin (updater app art,
  outside A1's four surfaces) — noted, not changed.
