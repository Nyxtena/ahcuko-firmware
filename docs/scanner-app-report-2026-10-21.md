# Athena Scanner v0.1.0 — build report (2026-10-21)

Built to Jacob's ratified direction (`files/scanner-app-direction-2026-10-21.md` in the goal workspace):

- If Athena asked Jacob to scan something, or Jacob told Athena he was going to scan something he wanted her to look at — Athena activates the scanner, Jacob scans it, the data is stored, and also pushed back to Athena.
- If Jacob scans something without either condition — he gets the same readout, and on the readout the Up button sends it to Athena instantly, and the More menu includes **Send to Athena** at the top.

## Artifact

- FAP `athena_scanner` (Tools), 13,784 B, MD5 `018d276518134efc6496f93448c9cbad`, APPCHK Target 7 / API 87.1. Undefined-symbol sweep vs pinned `api_symbols.csv`: 55 undefined, 0 not exported.
- Sources: `apps/athena_scanner/` (athena_scanner.c, application.fam, icon.png). RPC plumbing + the SessionClose unregister-first discipline carried over from Athena Hub v0.1.2.
- Store: `/ext/apps_data/athena_scanner/records.jsonl` (append-only; record lines + `pushed` / `send_marked` marker lines).
- Link surface (staged mode): `hello`, `scan.status`, `scan.start`; unsolicited push envelope type `scan.record`. Solo-mode sends are marked on-device and delivered on Athena's next pull.
- Detection: `nfc_scanner` → most-derived protocol by parent-chain depth → single-shot poller read; full UID/ATQA/SAK extraction for the ISO14443-3A family (3a/4a, MIFARE Classic/Ultralight/DESFire), protocol-only records otherwise. No emulation, no transmission of card data; observe + own-store only.

## Live verification (2026-10-21, Jacob's hand + Athena's staging)

- Self-test: deploy MD5 verified; staged hello (`app: athena_scanner, records: 0`); `scan.status` → scanning; clean app exit; fresh session healthy; solo launch renders the scanning screen.
- **Staged Tangem scan (06:23:07 CDT):** Athena staged the app; Jacob tapped his Tangem card; record #1 stored (ISO14443-4A, UID 61 DE 60 7D, ATQA 04 00, SAK 20) and pushed to Athena live in the same moment (`scan.record` received at the host; device status `last_pushed: true`; `{"id":1,"event":"pushed"}` marker in the store). Readout screen showed "Sent to Athena".
- **Solo Tangem scan (06:24:36 CDT):** Jacob launched the app himself, scanned, pressed Up → readout showed "Marked for Athena"; store shows record #2 + `{"id":2,"event":"send_marked"}`; Athena pulled the record in the same session.

## Detours worth keeping

- Two staged windows elapsed with no tap before Jacob was at the device — staging windows are only useful while he is present.
- The stock NFC app left open from the Tangem check held the app loader (App Start status 17 = ERROR_APP_SYSTEM_LOCKED) and does not answer RPC App Exit (21); it had to be walked out by injected keys, including its "All unsaved data will be lost" dialog.
- **InputKey enum correction (source-verified):** Up=0, Down=1, **Right=2, Left=3**, Ok=4, Back=5. The census-era scripts labeled 2 as "Left" — that value is Right; the one fob-setup transition credited to "Left" was a Right step (433→315 is +1 in that app's table). Driver scripts must use these values.

## Known nits (v0.1.1 polish, non-blocking)

- Scanning-screen title "ATHENA SCANNER" overlaps the version string.
- The pushed `scan.record` payload's own `pushed` field reads false (the payload JSON is composed before the push flag flips; the marker line and scan.status report the truth).
- Tech label shows "NFC-A" where the stock readout shows "ISO 14443-4 (NFC-A)".
