"""Reference scenario: one prayer fires exactly once.

Boot at 11:49:30 GMT (30 s before Dhuhr at 11:50:00) against a mock NTP server
that serves the same clock at 1x, so the device's own 1 Hz tick carries it
across the prayer -- and prove the scheduler does not double-fire the prayer it
is racing towards, nor replay the prayers that already passed (Fajr is
pre-marked and must stay silent).

The speed is 1x, not accelerated: the device's clock runs in real time and the
mock cannot make it run faster, so an accelerated mock only makes the landing
point drift while leaving the device stuck. At 1x, 30 fake seconds is 30 real
seconds and the scenario is honest.
"""

from scenario_runner import Scenario


class SinglePrayerFire(Scenario):
    name = "01_single_prayer_fire"
    start_utc = "2026-10-31 11:49:30"
    speed = 1.0
    quick = True
    total_timeout_s = 90.0

    # CONFIG COUPLING -- READ BEFORE EDITING:
    #
    # start_utc is chosen so that Dhuhr = 11:50:00 for THIS specific config.
    # Coordinates are Crawley, UK; method is Moonsighting Committee (6).
    # Empirical hardware verification (Session B, dev board, boot 2 log)
    # confirmed that this config produces dhuhr_sec = 42600 = 11:50:00 on
    # 31 Oct 2026. The scenario boots 30 s before, so Dhuhr is exactly
    # 30 s in the future -- outside scheduler_premark_past()'s grace
    # window, so it is not pre-marked as "already fired" and the 1 Hz
    # scheduler fires it live at fake 11:50:00.
    #
    # If you change any of latitude, longitude, or method, you MUST
    # recompute start_utc so Dhuhr is still 30 s after boot. Otherwise
    # the device's premark_past() will mark Dhuhr as already-fired and
    # the test will fail with "dhuhr_fire count=0" while the scheduler
    # is doing exactly the right thing. See the CR's harness diagnosis
    # (Session B) for the full failure mode.
    #
    # Session B's /debug/ntp extension seeds these values into svc_config
    # BEFORE the boot-2 NTP sync runs, so boot 2 calculates prayer times
    # from the scenario's own config and (with the seed below) lands at
    # the correct Dhuhr epoch.
    latitude = 51.13115396014885
    longitude = -0.1864782309679473
    method = 6  # SVC_CONFIG_METHOD_MOONSIGHTING

    description = (
        "Boot at fake 11:49:30 (30 s before Dhuhr at 11:50). Mock NTP "
        "serves 1x real-time. Expect: NTP sync, exactly ONE Dhuhr fire "
        "at ~t+30s, ZERO Fajr fire (pre-marked past), no duplicate "
        "in next 30s."
    )

    expected_monitor_lines = """\
I (...) svc_time: NTP sync ok (network): UTC 2026-10-31 11:49:XX -> local 11:49:XX
I (...) svc_prayer_eng_svc: prayer times updated (date=20261031 offset=0 min ...)
I (...) svc_scheduler: initialised
I (...) svc_scheduler: task started (1 Hz epoch poll, core 0)
I (...) svc_scheduler: Dhuhr adhan due (epoch ..., now ...)
<no Fajr fire, no duplicate Dhuhr fire>"""

    expects = [
        ("sync",        r"NTP sync ok \(network\): UTC 2026-10-31 11:49:\d{2}",
                        30.0, 1, 3),
        ("dhuhr_fire",  r"svc_scheduler: Dhuhr adhan due",  60.0, 1, 1),
        ("fajr_nofire", r"svc_scheduler: Fajr adhan due",   None,  0, 0),
    ]
