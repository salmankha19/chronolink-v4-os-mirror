# Prayer engine host tests

Run without ESP-IDF:  `make test`   (needs only g++; golden.csv is committed)

* test_golden.cpp   - engine vs PyEphem reference (gen_golden.py), 9 cities x ~122 dates x up to 7 events, tolerance 30 s.
                      Uses HighLatitudeRule::NONE so only the astronomy is tested.
* test_wrapper.cpp  - C API: unwrapped Isha/midnight, argument + date validation, Asr-yellow sentinel, Jafaria, polar day.
* test_invariants.cpp (make test-highlat) - 13,140 city/method/day combinations at 40..59.9 N with AUTO:
                      ordering, Isha/Fajr gaps, midnight range, day-to-day jumps.

Reference: PyEphem 4.2.1, pressure 0, sunrise/sunset -0.833 deg, twilight to sun centre. Fajr/Isha rows only where the angle is
reached with >= 1 deg to spare. Python is only needed to regenerate golden.csv.

Results on the PR 17-19 code: golden worst 20.1 s (Dhuhr; solar-model limit), everything else <= 11.1 s; wrapper all pass.
Original (pre-PR 19) code: worst 343 s, 1,655 rows over tolerance.
