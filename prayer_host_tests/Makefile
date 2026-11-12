# Host-side prayer engine tests (no ESP-IDF needed; needs only g++ + a C
# compiler for the pure scheduler logic and the jamat module).
#   make test            golden (vs PyEphem) + C-wrapper + scheduler + jamat differential -> run in CI
#   make test-highlat    year-long invariants at 40..59.9 N, all methods      -> known findings, see README
#   make golden          regenerate golden.csv (needs: pip install ephem scipy)
COMP ?= ../components/svc_prayer_engine
CXX  ?= g++
CXXFLAGS ?= -std=gnu++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -I$(COMP)/include -Istubs
LIBSRC = $(COMP)/src/prayertimes/PrayerTimes.cpp
SCHED ?= ../components/svc_scheduler
SCHEDFLAGS = -std=gnu11 -O2 -Wall -Wextra -I$(SCHED)/include
SCHEDSRC = $(SCHED)/src/scheduler_logic.c
JAMAT ?= ../components/svc_prayer_engine_service
JAMATFLAGS = -std=gnu11 -O2 -Wall -Wextra \
             -I$(JAMAT)/include \
             -I$(JAMAT)/src \
             -I$(COMP)/include \
             -I../components/svc_config/include \
             -Istubs
JAMATINC = -I$(JAMAT)/include -I$(JAMAT)/src -I../components/svc_config/include
OUT ?= build

all: test
$(OUT):; mkdir -p $(OUT)
$(OUT)/test_golden: test_golden.cpp $(LIBSRC) | $(OUT);         $(CXX) $(CXXFLAGS) $^ -o $@
$(OUT)/test_invariants: test_invariants.cpp $(LIBSRC) | $(OUT); $(CXX) $(CXXFLAGS) $^ -o $@
$(OUT)/test_wrapper: test_wrapper.cpp $(COMP)/src/svc_prayer_engine.cpp $(LIBSRC) | $(OUT); $(CXX) $(CXXFLAGS) $^ -o $@
$(OUT)/scheduler_logic.o: $(SCHEDSRC) $(SCHED)/include/scheduler_logic.h | $(OUT); $(CC) $(SCHEDFLAGS) -c $< -o $@
$(OUT)/test_scheduler: test_scheduler.cpp $(OUT)/scheduler_logic.o | $(OUT);    $(CXX) $(CXXFLAGS) -I$(SCHED)/include $^ -o $@
$(OUT)/jamat_calculator.o: $(JAMAT)/src/jamat_calculator.c | $(OUT);            $(CC) $(JAMATFLAGS) -c $< -o $@
$(OUT)/test_jamat_differential: test_jamat_differential.cpp $(OUT)/jamat_calculator.o | $(OUT); $(CXX) $(CXXFLAGS) $(JAMATINC) $^ -o $@
$(OUT)/merge_logic.o: $(JAMAT)/src/merge_logic.c $(JAMAT)/src/merge_logic.h | $(OUT); $(CC) -std=gnu11 -O2 -Wall -Wextra -I$(JAMAT)/src -c $< -o $@
$(OUT)/test_merge_logic: test_merge_logic.cpp $(OUT)/merge_logic.o | $(OUT);    $(CXX) $(CXXFLAGS) -I$(JAMAT)/src $^ -o $@
$(OUT)/test_moonsighting: test_moonsighting.cpp $(LIBSRC) | $(OUT); $(CXX) $(CXXFLAGS) $^ -o $@
$(OUT)/test_mosque_calendars: test_mosque_calendars.cpp $(COMP)/src/svc_prayer_engine.cpp $(LIBSRC) | $(OUT); $(CXX) $(CXXFLAGS) $^ -o $@
$(OUT)/hijri_calendar.o: $(JAMAT)/../svc_config/src/hijri_calendar.c $(JAMAT)/../svc_config/src/hijri_calendar.h | $(OUT); $(CC) -std=gnu11 -O2 -Wall -Wextra -I$(JAMAT)/../svc_config/src -c $< -o $@
$(OUT)/test_hijri_calendar: test_hijri_calendar.cpp $(OUT)/hijri_calendar.o | $(OUT); $(CXX) $(CXXFLAGS) -I$(JAMAT)/../svc_config/src $^ -o $@
$(OUT)/ramadan_window_validator.o: $(JAMAT)/../svc_config/src/ramadan_window_validator.c $(JAMAT)/../svc_config/src/ramadan_window_validator.h | $(OUT); $(CC) -std=gnu11 -O2 -Wall -Wextra -I$(JAMAT)/../svc_config/src -c $< -o $@
$(OUT)/test_ramadan_window_validator: test_ramadan_window_validator.cpp $(OUT)/ramadan_window_validator.o | $(OUT); $(CXX) $(CXXFLAGS) -I$(JAMAT)/../svc_config/src $^ -o $@
$(OUT)/svc_config_ramadan_resolution.o: $(JAMAT)/../svc_config/src/svc_config_ramadan_resolution.c $(JAMAT)/../svc_config/include/svc_config.h $(JAMAT)/../svc_config/src/hijri_calendar.h | $(OUT); $(CC) -std=gnu11 -O2 -Wall -Wextra -I$(JAMAT)/../svc_config/include -I$(JAMAT)/../svc_config/src -Istubs -c $< -o $@
$(OUT)/test_config_ramadan_resolution: test_config_ramadan_resolution.cpp $(OUT)/svc_config_ramadan_resolution.o $(OUT)/hijri_calendar.o | $(OUT); $(CXX) $(CXXFLAGS) -I$(JAMAT)/../svc_config/include $^ -o $@

golden:; python3 gen_golden.py > golden.csv
test: $(OUT)/test_golden $(OUT)/test_wrapper $(OUT)/test_scheduler $(OUT)/test_jamat_differential $(OUT)/test_merge_logic $(OUT)/test_moonsighting $(OUT)/test_mosque_calendars $(OUT)/test_hijri_calendar $(OUT)/test_ramadan_window_validator $(OUT)/test_config_ramadan_resolution
	@echo "=== golden (vs PyEphem, tol 30 s) ===";  $(OUT)/test_golden golden.csv 30
	@echo; echo "=== C wrapper ==="; $(OUT)/test_wrapper
	@echo; echo "=== scheduler logic ==="; $(OUT)/test_scheduler
	@echo; echo "=== jamat differential ==="; $(OUT)/test_jamat_differential
	@echo; echo "=== merge logic (jamat resolve) ==="; $(OUT)/test_merge_logic
	@echo; echo "=== mosque calendars (fixtures) ==="; $(OUT)/test_mosque_calendars
	@echo; echo "=== Moonsighting vs adhan-js (tol 90 s) ==="; $(OUT)/test_moonsighting moonsighting_adhan.csv 90
	@echo; echo "=== hijri calendar (UAQ Ramadan windows) ==="; $(OUT)/test_hijri_calendar
	@echo; echo "=== ramadan window validator ==="; $(OUT)/test_ramadan_window_validator
	@echo; echo "=== svc_config_is_ramadan (resolution) ==="; $(OUT)/test_config_ramadan_resolution

test-highlat: $(OUT)/test_invariants
	@$(OUT)/test_invariants
clean:; rm -rf $(OUT)
.PHONY: all golden test test-highlat clean
