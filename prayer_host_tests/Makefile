# Host-side prayer engine tests (no ESP-IDF needed; needs only g++).
#   make test            golden (vs PyEphem reference) + C-wrapper tests      -> run in CI
#   make test-highlat    year-long invariants at 40..59.9 N, all methods      -> known findings, see README
#   make golden          regenerate golden.csv (needs: pip install ephem scipy)
COMP ?= ../components/svc_prayer_engine
CXX  ?= g++
CXXFLAGS ?= -std=gnu++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -I$(COMP)/include -Istubs
LIBSRC = $(COMP)/src/prayertimes/PrayerTimes.cpp
OUT ?= build

all: test
$(OUT):; mkdir -p $(OUT)
$(OUT)/test_golden: test_golden.cpp $(LIBSRC) | $(OUT);         $(CXX) $(CXXFLAGS) $^ -o $@
$(OUT)/test_invariants: test_invariants.cpp $(LIBSRC) | $(OUT); $(CXX) $(CXXFLAGS) $^ -o $@
$(OUT)/test_wrapper: test_wrapper.cpp $(COMP)/src/svc_prayer_engine.cpp $(LIBSRC) | $(OUT); $(CXX) $(CXXFLAGS) $^ -o $@
golden:; python3 gen_golden.py > golden.csv
test: $(OUT)/test_golden $(OUT)/test_wrapper
	@echo "=== golden (vs PyEphem, tol 30 s) ===";  $(OUT)/test_golden golden.csv 30
	@echo; echo "=== C wrapper ==="; $(OUT)/test_wrapper
test-highlat: $(OUT)/test_invariants
	@$(OUT)/test_invariants
clean:; rm -rf $(OUT)
.PHONY: all golden test test-highlat clean
