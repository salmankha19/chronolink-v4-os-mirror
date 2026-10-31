#!/usr/bin/env python3
"""patterns.py -- regex expectations for console output.

Pure Python only: no serial, no requests, no hardware. That is what lets the
gate unit tests (double-fire, no-fire) run on a machine with no board
attached, and it keeps the pass/fail decision testable in isolation from the
transport that produced the lines.

A scenario declares expectations as plain tuples and this module turns them
into ExpectSpec/MatchState pairs:

    specs  = compile_specs(scenario.expects)
    states = [MatchState() for _ in specs]
    feed_line(specs, states, line, fake_now)     # per console line
    status, reasons = evaluate(specs, states, fake_now, start_fake)
"""

from __future__ import annotations

import re
from typing import NamedTuple, Optional

# Keep the postmortem small: the 11th matching line tells us nothing new.
MATCHED_LINES_CAP = 10


class ExpectSpec(NamedTuple):
    label: str
    pattern: str
    deadline_s: Optional[float]
    min_count: int
    max_count: Optional[int]   # None = unbounded


class MatchState:
    """Running tally for one ExpectSpec, fed one console line at a time."""

    def __init__(self) -> None:
        self.count = 0
        self.first_match_fake_epoch: Optional[float] = None
        self.matched_lines: list = []   # cap at 10 entries


def compile_specs(raw: list) -> list:
    """Turn raw expectation tuples into ExpectSpec objects.

    Elements are tuples of length 3, 4, or 5:
        (label, pattern, deadline_s)
        (label, pattern, deadline_s, min_count)
        (label, pattern, deadline_s, min_count, max_count)
    Defaults: min_count=1, max_count=min_count.

    deadline_s is measured in FAKE seconds from the scenario's start_utc, so
    an accelerated clock reaches it quickly. None = no deadline.
    """
    specs = []
    for element in raw:
        if len(element) == 3:
            label, pattern, deadline_s = element
            min_count, max_count = 1, 1
        elif len(element) == 4:
            label, pattern, deadline_s, min_count = element
            max_count = min_count
        elif len(element) == 5:
            label, pattern, deadline_s, min_count, max_count = element
        else:
            raise ValueError(
                "expectation needs 3, 4 or 5 elements, got %d: %r"
                % (len(element), element)
            )
        specs.append(
            ExpectSpec(
                str(label),
                str(pattern),
                None if deadline_s is None else float(deadline_s),
                int(min_count),
                None if max_count is None else int(max_count),
            )
        )
    return specs


def feed_line(specs, states, line, fake_now_epoch, verbose: bool = False) -> None:
    """Match one console line against every spec and update the tallies."""
    if len(states) != len(specs):
        raise ValueError(
            "states/specs length mismatch: %d vs %d" % (len(states), len(specs))
        )

    for spec, state in zip(specs, states):
        if not re.search(spec.pattern, line):
            continue
        state.count += 1
        if state.first_match_fake_epoch is None:
            state.first_match_fake_epoch = fake_now_epoch
        if len(state.matched_lines) < MATCHED_LINES_CAP:
            state.matched_lines.append(line)
        if verbose:
            print(
                "  [match] %-12s #%d @ fake %+.1fs | %s"
                % (spec.label, state.count, fake_now_epoch, line)
            )


def evaluate(specs, states, fake_now_epoch, start_fake_epoch,
             verbose: bool = False) -> tuple:
    """Return (status, reasons) where status is PASS, FAIL or PENDING.

    FAIL as soon as a count is above its maximum, or below its minimum after
    that spec's deadline has passed -- neither can recover, so reporting it
    immediately shortens the run.
    PASS only once the fake clock is past every finite deadline and every
    count is inside [min, max]. Anything else is PENDING.
    """
    if len(states) != len(specs):
        raise ValueError(
            "states/specs length mismatch: %d vs %d" % (len(states), len(specs))
        )

    elapsed = fake_now_epoch - start_fake_epoch
    reasons = []

    for spec, state in zip(specs, states):
        over_max = spec.max_count is not None and state.count > spec.max_count
        under_min = (
            spec.deadline_s is not None
            and elapsed >= spec.deadline_s
            and state.count < spec.min_count
        )
        if over_max or under_min:
            reasons.append(_reason(spec, state))

    if reasons:
        if verbose:
            print("  [evaluate] FAIL after fake %+.1fs" % elapsed)
        return "FAIL", reasons

    deadlines = [s.deadline_s for s in specs if s.deadline_s is not None]
    in_range = all(
        state.count >= spec.min_count
        and (spec.max_count is None or state.count <= spec.max_count)
        for spec, state in zip(specs, states)
    )
    if deadlines and in_range and elapsed >= max(deadlines):
        if verbose:
            print("  [evaluate] PASS after fake %+.1fs" % elapsed)
        return "PASS", []

    return "PENDING", []


def _reason(spec, state) -> str:
    """Human-readable one-liner for a failing spec."""
    upper = "unbounded" if spec.max_count is None else spec.max_count
    return "expected %d-%s matches for '%s', got %d" % (
        spec.min_count,
        upper,
        spec.label,
        state.count,
    )
