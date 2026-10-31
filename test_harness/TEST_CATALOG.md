# ChronoLink Test Catalog

Live document. Update with every scenario add/remove/change.
`run_all.py --check-catalog` fails if this file drifts from `scenarios/*.py`.

## Available (implemented)

| ID | Scenario | Purpose | Guards against | Speed | Duration | Status |
|---|---|---|---|---|---|---|
| 01 | single_prayer_fire | Fires Dhuhr exactly once at correct epoch | Double-fire, missed-fire | 1x | ~90s | ✅ v1 |

### 01_single_prayer_fire
- **Purpose:** Prove the 1 Hz scheduler publishes `Dhuhr adhan due` exactly once when the mocked clock lands 30 s before Dhuhr, and never replays Fajr.
- **Setup:** Test firmware (`env:single_mcu_test`) on the board; mock NTP server serving `2026-10-31 11:49:30` UTC at 1x (anchored at the board's first request, so the landing point is exact); `POST /debug/ntp` points the device's SNTP client at the PC.
- **Timing:** The device's clock runs at 1x, so 30 fake seconds = 30 real seconds. The fire lands at ~t+30 s wall clock; PASS needs 60 s of fake clock, i.e. ~65 s wall clock, inside the 90 s timeout.
- **Proves:** NTP sync reaches the board, prayer times are published for the day, and `EVENT_PRAYER_TIME_REACHED` is published exactly once for Dhuhr with no Fajr replay.
- **Guards against:** Double-fire (repeated local hour on DST fall-back, backwards NTP step) and missed-fire (an NTP step skipping the epoch comparison).
- **Expected monitor output:** see `expected_output/01_single_prayer_fire.md`
- **Run:** `python run_all.py --port COM7 --only 01_single_prayer_fire`

## Planned (v2+)

| ID | Scenario | Purpose | Status |
|---|---|---|---|
| 02 | full_day | All 5 prayers fire in order | ⏳ v2 |
| 03 | dst_spring_forward | No skip through DST | ⏳ v2 |
| 04 | dst_fall_back | No double-fire through DST | ⏳ v2 |
| 05 | midday_boot | Pre-mark past on boot | ⏳ v2 |
| 06 | config_change | PRAYER_TIMES_UPDATED resets state | ⏳ v2 |

## Blocked (needs hardware)

| ID | Scenario | Blocked by |
|---|---|---|
| 07 | display_frame_dispatch | ST7796S (~2 weeks) |
| 08 | audio_adhan_playback | MAX98357A (~2 weeks) |
