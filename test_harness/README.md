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
python run_all.py --port COM7 --save-log          # keep run.log + result.txt on PASS
```

## Preserving logs on PASS
By default, PASS scenarios write no log files (keeps `results/` small). Use
`--save-log` to preserve `run.log` + `result.txt` on PASS.
`pre_push_check.ps1` uses `--save-log` automatically.

Each PASS writes `results/<YYYYMMDD_HHMMSS>/<scenario_name>/` holding just
those two files — no `expected.txt` and no `diff.txt`, which only mean
something when a run failed to match. FAIL / ERROR still write the full
postmortem regardless of the flag.

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
6. Runner restores NVS slot 0 to the firmware default before closing the
   port, so neither the next test boot nor a production boot syncs from
   this PC (see **NVS cleanup**).

## NVS cleanup

`/debug/ntp` persists whatever host it is given into NVS slot 0. Left
alone, a board therefore keeps syncing from the harness machine on every
later boot — including boots of production firmware, which has no
`/debug/ntp` route to undo it.

The runner undoes the write itself. `ScenarioRunner.run()` sets a local
`ntp_host_set` flag immediately *before* calling
`controller.set_ntp_host()`, and its `finally:` block calls
`controller.restore_ntp_host()` *before* closing the serial port. Setting
the flag first matters: the injection call can raise on a read timeout
after the host is already in NVS, and that path still has to be undone.

- `restore_ntp_host()` GETs `/debug/ntp?host=time.google.com` with
  `timeout_s=12.0` and no retries. The endpoint blocks up to 10 s on the
  follow-up sync (`svc_time_sync_ntp_once`), and the previous sync is
  normally older than the 60 s resync floor by the time a scenario ends,
  so that sync does run — a 5 s timeout would warn about a restore that
  actually succeeded.
- It returns **True** on HTTP 200 *and* on the 500 whose body is
  `sync_failed` / `ESP_ERR_INVALID_STATE`. `svc_time` writes the host to
  NVS before it attempts the sync, so a skipped or failed sync still
  leaves slot 0 back on the firmware default.
- It returns **False** on anything else (no board IP, refused, read
  timeout, `400 bad_host`, `500 nvs_unavailable`,
  `500 set_ntp_server_failed`) and never raises, so a failed cleanup
  cannot replace the run's real `status`/`reasons`/`error`.

Either outcome leaves the scenario result unchanged; only the log line
differs — `restored NVS slot 0 to time.google.com`, or
`WARN: could not restore NVS slot 0`. Pre-push runs and single-scenario
runs both go through `ScenarioRunner.run()`, so both restore; no manual
step is needed after a test session.

If you do see `WARN: could not restore NVS slot 0`, the board answered
but the write was not confirmed. Restore it by hand with
`curl http://<esp32>/debug/ntp?host=time.google.com`, or wipe NVS with
`pio run -e single_mcu -t erase` when the board is unreachable.

A run that never reaches its `finally:` block — Ctrl-C, adapter
unplugged, board losing Wi-Fi mid-run — needs the same manual command.

## Troubleshooting
| Symptom | Fix |
|---|---|
| `404` on `/debug/ntp` | Test firmware not flashed — run `pio run -e single_mcu_test -t upload` |
| No NTP sync | Port 123 UDP blocked — run mock server as admin |
| Serial port busy | Close `pio device monitor` first |
| Fires don't happen | An accelerated mock does not speed the device up: its clock runs at 1x, and at 600x a boot spread of a few seconds moves the landing point by tens of minutes. Run fire scenarios at `speed = 1.0` (see `scenarios/01_single_prayer_fire.py`) |
| `WinError 10013` binding port 123 | W32Time owns it. `net stop w32time` (admin). Restore with `net start w32time`. |
| After test session, NTP syncs to wrong host | Should not happen: the runner restores slot 0 itself (see **NVS cleanup**). If it does, the run was killed before cleanup ran — `pio run -e single_mcu -t erase` (wipes NVS) or `curl http://<esp32>/debug/ntp?host=time.google.com` |

## Safety
- `CHRONOLINK_TEST_HOOKS` flag exists **only in `single_mcu_test` env**.
  Production build strips the endpoint (verified by `strings firmware.bin | findstr debug/ntp`).
- Mock NTP only on LAN; not exposed to internet.
