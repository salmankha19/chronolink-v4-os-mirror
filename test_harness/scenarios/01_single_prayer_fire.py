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
    start_utc = "2026-10-31 11:44:30"
    speed = 1.0
    quick = True
    total_timeout_s = 90.0

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
        ("sync",        r"NTP sync ok",                     30.0, 1, 3),
        ("dhuhr_fire",  r"svc_scheduler: Dhuhr adhan due",  60.0, 1, 1),
        ("fajr_nofire", r"svc_scheduler: Fajr adhan due",   None,  0, 0),
    ]
