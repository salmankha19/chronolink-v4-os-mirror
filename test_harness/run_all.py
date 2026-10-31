#!/usr/bin/env python3
"""run_all.py -- discover and run the ChronoLink hardware test scenarios.

Three modes touch no hardware at all (--check-catalog, --list,
--dump-expectations), which is what makes them usable in CI and on a
developer machine with no board attached.

CLI (locked by AM5):
    python run_all.py --port COM7                     # all implemented scenarios
    python run_all.py --port COM7 --quick             # quick=True only
    python run_all.py --port COM7 --only prayer       # name substring filter
    python run_all.py --port COM7 --verbose           # interleave expected/actual
    python run_all.py --port COM7 --save-log          # keep run.log + result.txt on PASS
    python run_all.py --list
    python run_all.py --dump-expectations
    python run_all.py --check-catalog
"""

from __future__ import annotations

import argparse
import importlib.util
import logging
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

# Imported after the sys.path fix-up above so the harness modules resolve
# whether this file is run as a script or imported from the repo root.
from assertions.patterns import compile_specs            # noqa: E402
from scenario_runner import Scenario, ScenarioRunner      # noqa: E402

SCENARIOS_DIR = HERE / "scenarios"
CATALOG_PATH = HERE / "TEST_CATALOG.md"
EXPECTED_DIR = HERE / "expected_output"

AVAILABLE_HEADER = "## Available (implemented)"
CATALOG_ROW_RE = re.compile(r"^\|\s*(\d+)\s*\|\s*(\w+)\s*\|")
SCENARIO_FILE_RE = re.compile(r"^(\d+)_(.+)\.py$")


def _load_module(path: Path):
    """Import a scenario file by path.

    `import scenarios.01_x` is not legal Python -- module names may not start
    with a digit -- so scenario files are loaded through the import machinery
    directly rather than with an import statement.
    """
    spec = importlib.util.spec_from_file_location(path.stem, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load scenario module: %s" % path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def discover() -> list:
    """Return [(scenario_id, slug, instance, path)] sorted by file name.

    scenario_id/slug come from the file name: 01_single_prayer_fire.py ->
    ("01", "single_prayer_fire"), which is exactly what TEST_CATALOG.md lists.
    """
    found = []
    for path in sorted(SCENARIOS_DIR.glob("*.py")):
        if path.name.startswith("_"):
            continue
        match = SCENARIO_FILE_RE.match(path.name)
        if not match:
            raise RuntimeError(
                "scenario files must be named <NN>_<name>.py, found %s" % path.name
            )
        scenario_id, slug = match.group(1), match.group(2)

        module = _load_module(path)
        classes = [
            obj
            for obj in vars(module).values()
            if isinstance(obj, type)
            and issubclass(obj, Scenario)
            and obj is not Scenario
        ]
        if not classes:
            raise RuntimeError("%s declares no Scenario subclass" % path.name)

        for cls in classes:
            instance = cls()
            if instance.name != path.stem:
                print(
                    "warning: %s declares name=%r, expected %r"
                    % (path.name, instance.name, path.stem),
                    file=sys.stderr,
                )
            found.append((scenario_id, slug, instance, path))
    return found


def parse_catalog_rows(text: str) -> set:
    """Return {(id, name)} from the 'Available (implemented)' table only.

    Planned and Blocked tables are informational, so they are not parsed.
    """
    rows = set()
    in_available = False
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("## "):
            in_available = stripped == AVAILABLE_HEADER
            continue
        if not in_available:
            continue
        match = CATALOG_ROW_RE.match(stripped)
        if match:
            rows.add((match.group(1), match.group(2)))
    return rows


def select(args, scenarios: list) -> list:
    """Apply --quick and --only to a discovered scenario list."""
    selected = [entry for entry in scenarios if entry[2].quick or not args.quick]
    if args.only:
        needle = args.only.lower()
        selected = [
            entry
            for entry in selected
            if needle in entry[1].lower() or needle in entry[2].name.lower()
        ]
    return selected


def mode_check_catalog(scenarios: list) -> int:
    """Exit 0 when TEST_CATALOG.md and scenarios/ describe the same set."""
    if not CATALOG_PATH.exists():
        print("error: %s not found" % CATALOG_PATH, file=sys.stderr)
        return 1

    rows = parse_catalog_rows(CATALOG_PATH.read_text(encoding="utf-8"))
    discovered = {(entry[0], entry[1]) for entry in scenarios}

    missing_rows = discovered - rows           # implemented, not documented
    missing_code = rows - discovered           # documented as available, absent
    if not missing_rows and not missing_code:
        suffix = "" if len(rows) == 1 else "s"
        print("catalog in sync: %d scenario%s" % (len(rows), suffix))
        return 0

    print("catalog drift detected:", file=sys.stderr)
    for scenario_id, slug in sorted(missing_rows):
        print(
            "  implemented but missing from TEST_CATALOG.md: %s  %s"
            % (scenario_id, slug),
            file=sys.stderr,
        )
    for scenario_id, slug in sorted(missing_code):
        print(
            "  listed as available but not implemented: %s  %s"
            % (scenario_id, slug),
            file=sys.stderr,
        )
    return 1


def mode_list(scenarios: list) -> int:
    """Print every discovered scenario. No hardware."""
    for scenario_id, slug, scenario, path in scenarios:
        print("%s  %s" % (scenario_id, scenario.name))
        print("    file:        %s" % path.name)
        print("    quick:       %s" % scenario.quick)
        print("    start_utc:   %s" % scenario.start_utc)
        print("    speed:       %gx" % scenario.speed)
        print("    timeout:     %.0fs" % scenario.total_timeout_s)
        print("    description: %s" % scenario.description)
        print("    expected_monitor_lines:")
        for line in scenario.expected_monitor_lines.splitlines():
            print("      %s" % line)
        print("    expects:")
        for spec in compile_specs(scenario.expects):
            print(
                "      %-12s deadline=%-8s min=%-2d max=%-4s %s"
                % (
                    spec.label,
                    "none" if spec.deadline_s is None else "%g" % spec.deadline_s,
                    spec.min_count,
                    "inf" if spec.max_count is None else spec.max_count,
                    spec.pattern,
                )
            )
        print()
    return 0


def render_expectation_page(scenario_id: str, scenario) -> str:
    """Markdown page for expected_output/<name>.md."""
    rows = [
        "# %s" % scenario.name,
        "",
        scenario.description,
        "",
        "- catalog id: %s" % scenario_id,
        "- start_utc: %s UTC" % scenario.start_utc,
        "- speed: %gx" % scenario.speed,
        "- quick: %s" % scenario.quick,
        "- total_timeout_s: %.0f" % scenario.total_timeout_s,
        "",
        "## Expected monitor output",
        "",
        "```text",
        scenario.expected_monitor_lines.rstrip("\n"),
        "```",
        "",
        "## Assertions",
        "",
        "| Label | Pattern | Deadline (fake s) | Min | Max |",
        "|---|---|---|---|---|",
    ]
    for spec in compile_specs(scenario.expects):
        rows.append(
            "| %s | `%s` | %s | %d | %s |"
            % (
                spec.label,
                spec.pattern,
                "none" if spec.deadline_s is None else "%g" % spec.deadline_s,
                spec.min_count,
                "unbounded" if spec.max_count is None else spec.max_count,
            )
        )
    return "\n".join(rows) + "\n"


def mode_dump_expectations(scenarios: list) -> int:
    """Write expected_output/<name>.md for every scenario. No hardware."""
    EXPECTED_DIR.mkdir(parents=True, exist_ok=True)
    for scenario_id, slug, scenario, path in scenarios:
        target = EXPECTED_DIR / ("%s.md" % scenario.name)
        target.write_text(
            render_expectation_page(scenario_id, scenario), encoding="utf-8"
        )
        print("wrote %s" % target)
    return 0


def mode_run(args, scenarios: list) -> int:
    """Run the selected scenarios against the board; exit 0 only if all PASS."""
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)-7s %(name)s: %(message)s",
    )

    selected = select(args, scenarios)
    if not selected:
        print(
            "no scenarios matched (--quick=%s --only=%s)"
            % (args.quick, args.only),
            file=sys.stderr,
        )
        return 1

    runner = ScenarioRunner(args.port, esp_ip=args.esp_ip, verbose=args.verbose)
    failed = 0

    for scenario_id, slug, scenario, path in selected:
        print("")
        print("=" * 72)
        print("running %s" % scenario.name)
        print("=" * 72)
        result = runner.run(scenario)
        print(
            "%s  %s  (real %.1fs, fake +%.0fs)"
            % (
                result.status,
                scenario.name,
                result.real_elapsed_s,
                result.fake_elapsed_s,
            )
        )
        for reason in result.reasons:
            print("  %s" % reason)
        if result.error:
            print("  error: %s" % result.error)
        if result.status != "PASS":
            failed += 1
            directory = runner.write_results(result)
            print("  postmortem: %s" % directory)
        elif args.save_log:
            # PASS writes nothing by default; --save-log keeps the evidence
            # without the FAIL-shaped expected.txt / diff.txt pair.
            directory = runner.save_pass_log(result)
            print("  saved log: %s" % directory)

    print("")
    print("%d/%d scenario(s) passed" % (len(selected) - failed, len(selected)))
    return 0 if failed == 0 else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="run_all.py",
        description="Discover and run ChronoLink hardware test scenarios.",
    )
    parser.add_argument(
        "--port", default="COM7", help="serial port of the board (default: COM7)"
    )
    parser.add_argument(
        "--esp-ip",
        dest="esp_ip",
        default=None,
        metavar="X.X.X.X",
        help="Override auto-detected board IP (default: parse from boot log "
        "'Got IP' line)",
    )
    parser.add_argument(
        "--quick", action="store_true", help="only scenarios marked quick=True"
    )
    parser.add_argument(
        "--only", default=None, metavar="SUBSTR",
        help="only scenarios whose name contains SUBSTR",
    )
    parser.add_argument(
        "--verbose", action="store_true",
        help="interleave device output with the matches it produces",
    )
    parser.add_argument(
        "--list", dest="list_only", action="store_true",
        help="print scenarios without touching hardware",
    )
    parser.add_argument(
        "--save-log", dest="save_log", action="store_true",
        help="Preserve run.log and result.txt even on PASS "
        "(default: PASS writes nothing)",
    )
    parser.add_argument(
        "--dump-expectations", action="store_true",
        help="write expected_output/<name>.md without touching hardware",
    )
    parser.add_argument(
        "--check-catalog", action="store_true",
        help="verify TEST_CATALOG.md matches scenarios/ without touching hardware",
    )
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)

    try:
        scenarios = discover()
    except RuntimeError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1

    if not scenarios:
        print("error: no scenarios found in %s" % SCENARIOS_DIR, file=sys.stderr)
        return 1

    # No-hardware modes first: they never construct a ScenarioRunner, so they
    # cannot touch the serial port even by accident.
    if args.check_catalog:
        return mode_check_catalog(scenarios)
    if args.list_only:
        return mode_list(scenarios)
    if args.dump_expectations:
        return mode_dump_expectations(scenarios)
    return mode_run(args, scenarios)


if __name__ == "__main__":
    sys.exit(main())
