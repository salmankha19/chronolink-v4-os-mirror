# ChronoLink Test Harness

Time-injection test rig for ChronoLink V4 OS. Runs the ESP32 through any
date, time, or DST transition in seconds — no waiting for real prayer times.

## Prerequisites
- Python 3.11+
- `pip install -r test_harness/requirements.txt`
- ESP32-S3 DevKit on `COM7` (override with `--port`)
- **Test firmware** built with `pio run -e single_mcu_test -t upload`
  (production firmware `single_mcu` has the debug endpoint stripped)

## Quick start
```powershell
# 1. Flash test firmware (once)
pio run -e single_mcu_test -t upload

# 2. Run quick regression (~3 min, no hardware waiting)
python test_harness/run_all.py --port COM7 --quick
```

## Available commands
```powershell
python run_all.py --list                          # what would run
python run_all.py --check-catalog                 # verify catalog sync
python run_all.py --dump-expectations             # generate expected_output/*.md
python run_all.py --port COM7 --quick             # fast regression (~3 min)
python run_all.py --port COM7 --only dst          # filter by name
python run_all.py --port COM7 --verbose           # interleaved expected/actual
```

## Pre-push regression
```powershell
.\test_harness\pre_push_check.ps1
```
Builds test firmware, runs `--quick`, exits non-zero on any failure.

## Adding a scenario
1. Create `scenarios/NN_name.py` (see `01_single_prayer_fire.py`)
2. Add a row to `TEST_CATALOG.md` under "Available"
3. Add a detail block below the table
4. Run `python run_all.py --check-catalog` — must exit 0

## How it works
1. Mock NTP server serves fake time, anchored at the device's first
   request (fire scenarios run at 1x — see Troubleshooting).
2. Device controller resets ESP32, reads the board IP from the serial
   log, and points its NTP at the PC via `/debug/ntp?host=<ip>`.
3. Runner resets a **second** time. `svc_time` refuses a manual sync
   within 60 s of the previous one (`sync skipped (manual)`), so on a
   device that has never been tested — or after a factory reset, when
   slot 0 is still `time.google.com` — the injection cannot move the
   clock. Slot 0 is persisted, so the second boot's automatic sync
   lands on the mock and the device really boots at the scenario's
   `start_utc`. The first boot's sync line is discarded; only the
   second one counts as the `sync` expectation.
4. Runner tails serial log, matches against scenario's `expects[]`.
   The clock origin is the epoch the mock actually served (printed as
   `<peer>@<epoch>`), so a stray early request cannot skew deadlines.
5. PASS = all patterns matched within deadlines, no unexpected fires.

## Troubleshooting
| Symptom | Fix |
|---|---|
| `404` on `/debug/ntp` | Test firmware not flashed — run `pio run -e single_mcu_test -t upload` |
| No NTP sync | Port 123 UDP blocked — run mock server as admin |
| Serial port busy | Close `pio device monitor` first |
| Fires don't happen | An accelerated mock does not speed the device up: its clock runs at 1x, and at 600x a boot spread of a few seconds moves the landing point by tens of minutes. Run fire scenarios at `speed = 1.0` (see `scenarios/01_single_prayer_fire.py`) |
| `WinError 10013` binding port 123 | W32Time owns it. `net stop w32time` (admin). Restore with `net start w32time`. |
| After test session, NTP syncs to wrong host | Test firmware wrote mock host to NVS slot 0. Restore: `pio run -e single_mcu -t erase` (wipes NVS) or `curl http://<esp32>/debug/ntp?host=time.google.com` |

## Safety
- `CHRONOLINK_TEST_HOOKS` flag exists **only in `single_mcu_test` env**.
  Production build strips the endpoint (verified by `strings firmware.bin | findstr debug/ntp`).
- Mock NTP only on LAN; not exposed to internet.
