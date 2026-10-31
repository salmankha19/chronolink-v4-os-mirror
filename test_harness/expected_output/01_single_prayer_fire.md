# 01_single_prayer_fire

Boot at fake 11:49:30 (30 s before Dhuhr at 11:50). Mock NTP serves 1x real-time. Expect: NTP sync, exactly ONE Dhuhr fire at ~t+30s, ZERO Fajr fire (pre-marked past), no duplicate in next 30s.

- catalog id: 01
- start_utc: 2026-10-31 11:49:30 UTC
- speed: 1x
- quick: True
- total_timeout_s: 90

## Expected monitor output

```text
I (...) svc_time: NTP sync ok (network): UTC 2026-10-31 11:49:XX -> local 11:49:XX
I (...) svc_prayer_eng_svc: prayer times updated (date=20261031 offset=0 min ...)
I (...) svc_scheduler: initialised
I (...) svc_scheduler: task started (1 Hz epoch poll, core 0)
I (...) svc_scheduler: Dhuhr adhan due (epoch ..., now ...)
<no Fajr fire, no duplicate Dhuhr fire>
```

## Assertions

| Label | Pattern | Deadline (fake s) | Min | Max |
|---|---|---|---|---|
| sync | `NTP sync ok` | 30 | 1 | 3 |
| dhuhr_fire | `svc_scheduler: Dhuhr adhan due` | 60 | 1 | 1 |
| fajr_nofire | `svc_scheduler: Fajr adhan due` | none | 0 | 0 |
