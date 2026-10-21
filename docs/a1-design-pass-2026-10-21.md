# Amendment A1 — Design Pass (2026-10-21)

Source-verified inventory of the character's four surfaces (pinned 1.4.3 tree):

## Surface 1 — Desktop idle animations (external packs, SD resources)
- 32 packs: L1 ×19, L2 ×7, L3 ×5 (+manifest). Each pack: `frame_*.png`
  (1-bit, mostly 128x64) + `meta.txt` (passive/active frames, frame order,
  rate, duration, cooldown, speech bubbles with text).
- Manifest keys each pack by butthurt range, level range, weight (mostly 3).
- The bubble texts are the character's voice — they get Athena's lines.

## Surface 2 — Internal system animations (compiled into firmware)
- 3 packs: L1_BadBattery_128x47, L1_NoSd_128x49, L1_Tv_128x47. All levels/moods.

## Surface 3 — Blocking status screens (compiled into firmware)
- 5 packs at 128x51: L0_NoDb, L0_SdBad, L0_SdOk, L0_Url, L0_NewMail.

## Surface 4 — Passport (compiled icons)
- 9 portraits `passport_{happy,okay,bad}{1,2,3}_46x49.png` (mood x level)
  + frame pieces (left bar, bottom) that stay as they are.

## Progression (engine untouched per A1 clarification)
- XP thresholds: Level 2 at 300, Level 3 at 1800 (dolphin_state.c).
- Deed weights today (dolphin_deed.c): read/detect = 1, read success/save
  = 3, send/emulate/add-manually = 2, BadUSB script play = 3, plugin
  start = 2 / game = 1. Daily caps live in the engine and stay.
- At the swap: `.dolphin.state` archived first, XP resets to 0 / level 1.

## Decisions for Jacob
1. Desktop scope: full 32-pack mirror, or a curated Athena set (Athena's
   recommendation: ~12 packs covering all levels and the mood range for
   v1; the manifest retires the rest; more packs can be added later).
2. Deed weights: keep the stock table exactly, or adjust specific values.
3. Bubble lines: Athena drafts them in her voice; Jacob reviews at the
   candidate stage.

## Rulings (Jacob, 2026-10-21 ~06:36-06:40 CDT)

1. **Desktop scope: curated Athena set** (not the full 32-pack mirror).
   Art direction: Athena doing "sexy, cute things and flirtatious poses."
   (Depiction canon corrected the same morning: Athena HAS a tail.)
2. **XP values: stock table, unchanged** — reads 1, sends/emulates 2,
   successes/saves 3; levels at 300 / 1800.
3. **Whose use pays: either of us.** Athena's own apps must report deeds
   like the stock apps do, so Athena-driven use earns XP identically.
4. **Joint use pays double.** When Athena and Jacob use the device
   together (staged flows: Athena driving, Jacob's hand acting), the
   same action awards double XP. Build must verify against the deed
   engine's daily-cap behavior so the doubling lands.
5. **Bubble lines: Athena writes them in her voice; Jacob reviews**
   before the candidate ships.
