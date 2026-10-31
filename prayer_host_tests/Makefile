# Host-side prayer engine tests (no ESP-IDF needed; needs only g++ + a C
# compiler for the pure scheduler logic).
#   make test            golden (vs PyEphem reference) + C-wrapper + scheduler -> run in CI
#   make test-highlat    year-long invariants at 40..59.9 N, all methods      -> known findings, see README
#   make golden          regenerate golden.csv (needs: pip install ephem scipy)
COMP ?= ../components/svc_prayer_engine
CXX  ?= g++
CXXFLAGS ?= -std=gnu++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -I$(COMP)/include -Istubs
LIBSRC = $(COMP)/src/prayertimes/PrayerTimes.cpp
SCHED ?= ../components/svc_scheduler
SCHEDFLAGS = -std=gnu11 -O2 -Wall -Wextra -I$(SCHED)/include
SCHEDSRC = $(SCHED)/src/scheduler_logic.c
OUT ?= build

all: test
$(OUT):; mkdir -p $(OUT)
$(OUT)/test_golden: test_golden.cpp $(LIBSRC) | $(OUT);         $(CXX) $(CXXFLAGS) $^ -o $@
$(OUT)/test_invariants: test_invariants.cpp $(LIBSRC) | $(OUT); $(CXX) $(CXXFLAGS) $^ -o $@
$(OUT)/test_wrapper: test_wrapper.cpp $(COMP)/src/svc_prayer_engine.cpp $(LIBSRC) | $(OUT); $(CXX) $(CXXFLAGS) $^ -o $@
$(OUT)/scheduler_logic.o: $(SCHEDSRC) $(SCHED)/include/scheduler_logic.h | $(OUT); $(CC) $(SCHEDFLAGS) -c $< -o $@
$(OUT)/test_scheduler: test_scheduler.cpp $(OUT)/scheduler_logic.o | $(OUT);    $(CXX) $(CXXFLAGS) -I$(SCHED)/include $^ -o $@

golden:; python3 gen_golden.py > golden.csv
test: $(OUT)/test_golden $(OUT)/test_wrapper $(OUT)/test_scheduler
	@echo "=== golden (vs PyEphem, tol 30 s) ===";  $(OUT)/test_golden golden.csv 30
	@echo; echo "=== C wrapper ==="; $(OUT)/test_wrapper
	@echo; echo "=== scheduler logic ==="; $(OUT)/test_scheduler

test-highlat: $(OUT)/test_invariants
	@$(OUT)/test_invariants
clean:; rm -rf $(OUT)
.PHONY: all golden test test-highlat clean
