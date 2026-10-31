#!/usr/bin/env python3
"""scenario_runner.py -- run one Scenario against one board.

Sequence per scenario (docs/session5.7-prompt.md, Commit 4):

  1. resolve the PC's LAN IP -- that is the NTP host the board must sync from
  2. start the mock NTP server at the scenario's start_utc / speed
  3. open the serial port and hard-reset the board
  4. wait for the boot banner AND the board's own IP, discarding everything
     before the banner
  5. wait for svc_time's automatic post-DHCP NTP sync to finish
  6. point the device's SNTP client at the mock (controller retries 404/500)
  7. feed every console line to the assertion engine until PASS/FAIL/timeout

The fake clock used by the assertion engine is anchored at the moment step 6
succeeds, not at process start: the mock's first request (which is the boot
sync) always answers exactly start_utc, so from that point the device's clock
and the runner's fake clock agree -- at speed 1.0 they advance together.

The PC LAN IP is never used as the board's address: it is the address the
board reaches, not an address the board answers on.

Everything the harness learns lands in RunResult; write_results() turns a
non-PASS result into the postmortem set (AM6): run.log, expected.txt,
diff.txt, result.txt.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

from assertions.patterns import (
    ExpectSpec,
    MatchState,
    compile_specs,
    evaluate,
    feed_line,
)
from device_controller import DeviceController, detect_pc_lan_ip

HARNESS_DIR = Path(__file__).resolve().parent
MOCK_SERVER = HARNESS_DIR / "mock_ntp_server.py"
RESULTS_DIR = HARNESS_DIR / "results"

# Last line of app_main(): everything before it is boot noise.
BOOT_BANNER = "main: ChronoLink initialized successfully"
BOOT_TIMEOUT_S = 30.0

# The board's own address, read off the console once DHCP completes.
# svc_network logs it on the link-up path (svc_network.c: "Got IP: %s"); the
# esp_netif default handler line is the fallback for builds that do not log
# through svc_network. Order matters: svc_network wins.
IP_PATTERNS = (
    re.compile(r"svc_network: Got IP: (\d+\.\d+\.\d+\.\d+)"),
    re.compile(r"esp_netif_handlers: sta ip: (\d+\.\d+\.\d+\.\d+)"),
)

# EVENT_NETWORK_CONNECTED makes svc_time sync on its own the moment the lease
# lands, and svc_time_sync_ntp_once() serialises on s_sync_lock: seeding the
# mock while that auto-sync is in flight returns ESP_ERR_INVALID_STATE, not a
# sync from our server. So wait for the attempt to end first. Every completion
# path logs through this prefix ("NTP sync ok (...)", "NTP sync failed (...)",
# "NTP sync after connect failed: ..."); the "starting" line does not match it.
AUTO_SYNC_MARKER = "svc_time: NTP sync"
AUTO_SYNC_TIMEOUT_S = 15.0

# Give the mock server time to bind before the board starts asking.
MOCK_STARTUP_WAIT_S = 0.5
MOCK_TERMINATE_WAIT_S = 2.0


class Scenario:
    """Base class: one file in scenarios/, one instance, no constructor args."""

    name: str = ""
    start_utc: str = ""
    speed: float = 60.0
    quick: bool = False
    description: str = ""
    expected_monitor_lines: str = ""
    expects: list = []
    total_timeout_s: float = 600.0


@dataclass
class RunResult:
    scenario_name: str
    status: str                       # PASS / FAIL / ERROR
    reasons: list
    real_elapsed_s: float
    fake_elapsed_s: float
    raw_log: list
    expect_states: list
    error: Optional[str] = None


def parse_start_utc(value: str) -> float:
    """'YYYY-MM-DD HH:MM:SS' in UTC -> epoch seconds."""
    return datetime.strptime(value, "%Y-%m-%d %H:%M:%S").replace(
        tzinfo=timezone.utc
    ).timestamp()


def format_epoch(epoch: float) -> str:
    """Epoch seconds -> 'YYYY-MM-DD HH:MM:SS' UTC, for human-readable logs."""
    return datetime.fromtimestamp(epoch, timezone.utc).strftime("%Y-%m-%d %H:%M:%S")


class MockNTPSubprocess:
    """Spawns mock_ntp_server.py and keeps its output off the console.

    stdout carries one '<peer>@<fake_epoch>' line per NTP request, so it is
    drained by a thread instead of a pipe -- an undrained pipe would block the
    mock after ~64 KB and stall the scenario.
    """

    def __init__(self, start_utc: str, speed: float, port: int = 123) -> None:
        self.start_utc = start_utc
        self.speed = speed
        self.port = port
        self.requests: list = []
        self.stderr_lines: list = []
        self._proc: Optional[subprocess.Popen] = None
        self._threads = []

    def __enter__(self) -> "MockNTPSubprocess":
        env = dict(os.environ, PYTHONUNBUFFERED="1")
        self._proc = subprocess.Popen(
            [
                sys.executable,
                str(MOCK_SERVER),
                "--start", self.start_utc,
                "--speed", str(self.speed),
                "--port", str(self.port),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",      # the mock's own messages are UTF-8; the
                                   # Windows locale default would mojibake them
            env=env,
        )
        self._threads = [
            threading.Thread(
                target=self._drain,
                args=(self._proc.stdout, self.requests),
                daemon=True,
            ),
            threading.Thread(
                target=self._drain,
                args=(self._proc.stderr, self.stderr_lines),
                daemon=True,
            ),
        ]
        for thread in self._threads:
            thread.start()

        time.sleep(MOCK_STARTUP_WAIT_S)
        if self._proc.poll() is not None:
            # Died on startup: almost always the bind, i.e. W32Time holds
            # UDP:123 (AM9). Surface the child's own message verbatim.
            for thread in self._threads:
                thread.join(timeout=1.0)
            detail = "\n".join(self.stderr_lines).strip() or "(no stderr)"
            code = self._proc.returncode
            self.__exit__()
            raise RuntimeError(
                "mock NTP server exited with %s:\n%s\n"
                "If this is a bind failure, W32Time owns UDP:%d -- run "
                "'net stop w32time' in an elevated PowerShell first."
                % (code, detail, self.port)
            )
        return self

    def __exit__(self, *exc_info) -> None:
        proc = self._proc
        if proc is None:
            return
        try:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=MOCK_TERMINATE_WAIT_S)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=MOCK_TERMINATE_WAIT_S)
        finally:
            for thread in self._threads:
                thread.join(timeout=1.0)
            for stream in (proc.stdout, proc.stderr):
                if stream is not None:
                    try:
                        stream.close()
                    except OSError:
                        pass
            self._proc = None

    @property
    def request_count(self) -> int:
        return len(self.requests)

    @staticmethod
    def _drain(stream, sink: list) -> None:
        """Copy a child pipe into a list until EOF (thread body)."""
        if stream is None:
            return
        try:
            for line in stream:
                sink.append(line.rstrip("\r\n"))
        except (ValueError, OSError):
            pass       # pipe closed under us during shutdown


class ScenarioRunner:
    """Drives one board through one scenario and returns a RunResult."""

    def __init__(self, port: str, esp_ip: Optional[str] = None,
                 verbose: bool = False) -> None:
        self.port = port
        self.esp_ip = esp_ip
        self.verbose = verbose
        # Resolved during run(): --esp-ip if given, else parsed from the boot
        # log. Never the PC's LAN IP -- that is only the NTP host.
        self._board_ip: Optional[str] = None
        # write_results() receives only the RunResult, whose field set is
        # fixed, so the scenario/expectations of the last run are remembered
        # here for the postmortem renderers.
        self._last_scenario: Optional[Scenario] = None
        self._last_specs: list[ExpectSpec] = []

    def run(self, scenario: Scenario) -> RunResult:
        self._last_scenario = scenario
        self._last_specs = compile_specs(scenario.expects)
        specs = self._last_specs
        states = [MatchState() for _ in specs]

        raw_log: list = []
        start_fake = parse_start_utc(scenario.start_utc)
        fake_now = start_fake
        wall_start = time.monotonic()

        status, reasons, error = "ERROR", [], None
        controller: Optional[DeviceController] = None

        self._log(
            "scenario %s: start %s UTC, %gx, timeout %.0fs"
            % (
                scenario.name,
                format_epoch(start_fake),
                scenario.speed,
                scenario.total_timeout_s,
            )
        )

        try:
            ntp_host = detect_pc_lan_ip()
            self._log("PC LAN IP (NTP host): %s" % ntp_host)
            if self.esp_ip:
                self._log("using --esp-ip: %s" % self.esp_ip)

            with MockNTPSubprocess(scenario.start_utc, scenario.speed) as mock:
                controller = DeviceController(self.port, esp_ip=self.esp_ip)
                controller.connect()

                # -- Boot 1: DHCP, address discovery, NVS write ---------------
                # This boot's sync goes wherever slot 0 already points, which on
                # a clean device is 'time.google.com' and therefore *real* time.
                # Its completion line is not evidence of anything and is
                # discarded: only the sync in boot 2, which provably comes from
                # the mock, may satisfy a 'sync' expectation.
                controller.reset()
                self._log(
                    "waiting up to %.0fs for boot banner + Got IP (boot 1/2)"
                    % BOOT_TIMEOUT_S
                )

                board_ip = self._wait_for_boot(controller, raw_log)
                self._board_ip = board_ip
                # The address is only known once the lease lands, so it is
                # handed to the controller after connect() rather than at
                # construction. The controller stays a dumb transport.
                controller.esp_ip = board_ip
                self._wait_for_auto_sync(controller, raw_log)

                controller.set_ntp_host(ntp_host)
                self._log("device NTP host set to %s" % ntp_host)

                # -- Boot 2: the boot sync now comes from the mock ------------
                # svc_time refuses a manual sync within 60 s of the previous one
                # ("sync skipped (manual): last sync is younger than 60 s"), so
                # on a device whose slot 0 did not already point at this PC the
                # injection above cannot move the clock -- the device keeps
                # whatever it got from its own boot server and never reaches the
                # scenario's start_utc. Slot 0 is persisted, so rebooting is the
                # reliable way to make the device fetch *our* time: the
                # automatic post-DHCP sync now lands on the mock, the mock
                # answers its first request with exactly start_utc, and the
                # device really does boot at the scenario's start time.
                boot1_lines = len(raw_log)
                controller.reset()
                self._log(
                    "waiting up to %.0fs for boot banner + Got IP (boot 2/2)"
                    % BOOT_TIMEOUT_S
                )

                board_ip = self._wait_for_boot(controller, raw_log)
                self._board_ip = board_ip
                controller.esp_ip = board_ip
                auto_sync_line = self._wait_for_auto_sync(
                    controller, raw_log, since=boot1_lines
                )

                # Anchor the fake clock to the device's clock at the moment the
                # device is known to be talking to our mock, and take the start
                # epoch from what the mock actually served instead of trusting
                # start_utc: anything that queried the mock a few seconds
                # earlier (a previous run's slot 0, a stray retry) shifts the
                # fake clock by exactly that much, and the deadlines are
                # relative to the device's clock, not to ours.
                start_fake = self._served_fake_epoch(mock, start_fake)
                anchor = time.monotonic()

                if auto_sync_line is not None:
                    # The device is now pointed at the mock, so the sync it
                    # made at boot -- from the same mock -- is a legitimate
                    # 'sync' observation. Fed late rather than live so it can
                    # never satisfy the expectation before set_ntp_host() ran.
                    feed_line(specs, states, auto_sync_line, start_fake,
                              verbose=self.verbose)

                status, reasons, fake_now = self._assert_loop(
                    controller, scenario, specs, states, raw_log, start_fake,
                    anchor
                )
                self._log("mock NTP requests served: %d" % mock.request_count)
                if mock.stderr_lines:
                    self._log("mock NTP stderr: %s" % "; ".join(mock.stderr_lines))

        except RuntimeError as exc:
            status, reasons, error = "ERROR", [], str(exc)
        except Exception as exc:      # noqa: BLE001 -- never crash the suite
            status, reasons, error = "ERROR", [], "%s: %s" % (type(exc).__name__, exc)
        finally:
            if controller is not None:
                try:
                    controller.close()
                except Exception as exc:   # noqa: BLE001
                    self._log("controller close failed: %s" % exc)

        return RunResult(
            scenario_name=scenario.name,
            status=status,
            reasons=reasons,
            real_elapsed_s=time.monotonic() - wall_start,
            fake_elapsed_s=max(0.0, fake_now - start_fake),
            raw_log=raw_log,
            expect_states=states,
            error=error,
        )

    def _wait_for_boot(self, controller: DeviceController, raw_log: list) -> str:
        """Read until the boot banner AND the board's IP are both known.

        Returns the board IP. With --esp-ip the address is already known, so
        the banner alone is enough. Lines before the banner are kept in
        raw_log (the postmortem wants the full capture) but never reach the
        assertion engine -- that is what stops a boot-time 'NTP sync ok' from
        satisfying a 'sync' expectation before the harness has pointed the
        device at the mock.

        Raises RuntimeError when either half is still missing at the deadline:
        the banner is printed before DHCP completes, so a board that has not
        logged its own address cannot be reached over HTTP yet, and there is
        nothing useful to assert.
        """
        board_ip = self.esp_ip
        banner_seen = False
        svc_ip = None       # preferred: svc_network's own link-up log
        netif_ip = None     # fallback: the IDF default handler line

        for line in controller.read_lines(BOOT_TIMEOUT_S):
            raw_log.append(line)

            if not banner_seen and BOOT_BANNER in line:
                banner_seen = True
                self._log("boot banner: %s" % line)
            elif self.verbose and not banner_seen:
                print("  boot | %s" % line, flush=True)

            if svc_ip is None:
                match = IP_PATTERNS[0].search(line)
                if match:
                    svc_ip = match.group(1)
                    self._log("board IP (from svc_network): %s" % svc_ip)
            if netif_ip is None:
                match = IP_PATTERNS[1].search(line)
                if match:
                    netif_ip = match.group(1)
                    self._log("board IP (from netif handler): %s" % netif_ip)

            board_ip = self.esp_ip or svc_ip or netif_ip
            if banner_seen and board_ip is not None:
                return board_ip

        if banner_seen:
            raise RuntimeError(
                "Boot banner seen but no Got IP within %.0fs -- DHCP failed or "
                "svc_network did not connect. Check svc_network logs in run.log"
                % BOOT_TIMEOUT_S
            )
        raise RuntimeError(
            "Board IP unknown: no --esp-ip and no 'Got IP' line in serial log "
            "within %.0fs (boot banner '%s' not seen either). Check WiFi "
            "(SSID/password) or re-run with --esp-ip <ip>"
            % (BOOT_TIMEOUT_S, BOOT_BANNER)
        )

    def _wait_for_auto_sync(self, controller: DeviceController,
                            raw_log: list, since: int = 0) -> Optional[str]:
        """Let svc_time's automatic post-DHCP NTP sync finish before seeding.

        svc_network publishes EVENT_NETWORK_CONNECTED as soon as the lease
        lands, which starts a sync of its own roughly 2.5 s later. Seeding
        during it means svc_time_sync_ntp_once() answers ESP_ERR_INVALID_STATE
        (s_sync_lock held) and `/debug/ntp` returns 500 -- a failure that has
        nothing to do with the seed we want to inject.

        Returns the completion line ("svc_time: NTP sync ..."), or None if the
        attempt had not finished inside AUTO_SYNC_TIMEOUT_S. Never raises:
        overrunning only costs the wait, and device_controller.set_ntp_host()
        retries a 500 sync_failed anyway.

        The lines land in raw_log but not in the assertion engine: run() feeds
        the returned line only after set_ntp_host() succeeds, so a boot-time
        sync cannot satisfy a 'sync' expectation before the harness has
        pointed the device at the mock.

        `since` is an index into raw_log: only lines from there on count as
        *this* boot's sync. Boot 2 passes the pre-reset length so boot 1's
        completion line -- which came from whatever server the device had
        before the injection, usually real time -- is not mistaken for it.
        """
        for line in raw_log[since:]:
            if AUTO_SYNC_MARKER in line:
                self._log("auto-sync already completed: %s" % line)
                return line

        deadline = time.monotonic() + AUTO_SYNC_TIMEOUT_S
        for line in controller.read_lines(AUTO_SYNC_TIMEOUT_S):
            raw_log.append(line)
            if AUTO_SYNC_MARKER in line:
                self._log("auto-sync completed: %s" % line)
                return line
            if self.verbose:
                print("  autosync | %s" % line, flush=True)
            if time.monotonic() >= deadline:
                break

        self._log(
            "auto-sync did not complete in %.0fs, proceeding anyway"
            % AUTO_SYNC_TIMEOUT_S
        )
        return None

    def _served_fake_epoch(self, mock: MockNTPSubprocess,
                           fallback: float) -> float:
        """Return the fake epoch the mock answered with most recently.

        `mock_ntp_server.py` prints one '<peer>@<fake_epoch>' line per request.
        The last one is the value the device has just set its clock from, which
        makes it a more honest origin for the assertion clock than
        scenario.start_utc: a request that arrived earlier (a previous run's
        slot 0, a stray retry) shifts the fake clock by exactly that much, and
        the deadlines must follow the device's clock, not ours.
        """
        if not mock.requests:
            self._log(
                "mock served no request yet -- using the scenario start %s as "
                "the clock origin" % format_epoch(fallback)
            )
            return fallback

        line = mock.requests[-1]
        _peer, _sep, served = line.rpartition("@")
        try:
            epoch = float(served)
        except ValueError:
            self._log(
                "unparsable mock request line %r -- using the scenario start %s"
                % (line, format_epoch(fallback))
            )
            return fallback

        if abs(epoch - fallback) >= 1.0:
            self._log(
                "mock served %s, %.0fs into the scenario -- using the served "
                "epoch as the clock origin"
                % (format_epoch(epoch), epoch - fallback)
            )
        return epoch

    def _assert_loop(self, controller, scenario, specs, states, raw_log,
                     start_fake, anchor=None):
        """Feed console lines to the assertion engine until PASS/FAIL/timeout.

        `anchor` is the monotonic instant at which the device was pointed at the
        mock (i.e. when its clock became start_fake). The fake clock is measured
        from there: fake_now = start_fake + (now - anchor) * speed. Passing None
        falls back to the loop entry, which is what the offline tests use.
        """
        loop_start = time.monotonic() if anchor is None else anchor
        fake_now = start_fake
        status, reasons = "PENDING", []

        for line in controller.read_lines(scenario.total_timeout_s):
            raw_log.append(line)
            if self.verbose:
                print("  dev  | %s" % line, flush=True)
            fake_now = start_fake + (time.monotonic() - loop_start) * scenario.speed
            feed_line(specs, states, line, fake_now, verbose=self.verbose)
            status, reasons = evaluate(
                specs, states, fake_now, start_fake, verbose=self.verbose
            )
            if status in ("PASS", "FAIL"):
                break

        if status == "PENDING":
            # read_lines() ran out of wall clock: the scenario never reached
            # a verdict, which is a failure of the scenario, not of the rig.
            fake_now = start_fake + (time.monotonic() - loop_start) * scenario.speed
            status = "FAIL"
            reasons = [
                "timeout after %.0fs wall clock (fake +%.0fs); %s"
                % (
                    scenario.total_timeout_s,
                    fake_now - start_fake,
                    ", ".join(
                        "%s=%d" % (spec.label, state.count)
                        for spec, state in zip(specs, states)
                    ),
                )
            ]

        return status, reasons, fake_now

    def _log(self, message: str) -> None:
        print("[runner] %s" % message, flush=True)

    def write_results(self, result: RunResult) -> Path:
        """Write the postmortem for a non-PASS run and return its directory.

        Layout (results/ is git-ignored):
            results/<YYYYMMDD_HHMMSS>/<scenario_name>/run.log
                                                    /expected.txt
                                                    /diff.txt
                                                    /result.txt
        """
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        directory = RESULTS_DIR / stamp / result.scenario_name
        directory.mkdir(parents=True, exist_ok=True)

        log_text = "\n".join(result.raw_log)
        (directory / "run.log").write_text(
            log_text + ("\n" if log_text else ""), encoding="utf-8"
        )

        expected_text = self._expected_text()
        (directory / "expected.txt").write_text(expected_text + "\n", encoding="utf-8")
        (directory / "diff.txt").write_text(
            self._render_diff(result, expected_text), encoding="utf-8"
        )
        (directory / "result.txt").write_text(
            self._render_result(result), encoding="utf-8"
        )
        return directory

    def _expected_text(self) -> str:
        if self._last_scenario is None:
            return ""
        return self._last_scenario.expected_monitor_lines.rstrip("\n")

    def _start_fake(self) -> float:
        if self._last_scenario is None:
            return 0.0
        return parse_start_utc(self._last_scenario.start_utc)

    def _summary_rows(self, specs, states) -> list:
        """One line per expectation: count, allowed range, first match time."""
        start = self._start_fake()
        rows = []
        for spec, state in zip(specs, states):
            upper = "inf" if spec.max_count is None else spec.max_count
            if state.first_match_fake_epoch is None:
                first = "never"
            else:
                first = "+%.0fs" % (state.first_match_fake_epoch - start)
            rows.append(
                "  %-12s count=%d allowed=%d-%s first_match=%s"
                % (spec.label, state.count, spec.min_count, upper, first)
            )
        return rows

    def _render_diff(self, result: RunResult, expected_text: str) -> str:
        """Side-by-side: expected monitor lines against the lines that matched."""
        expected = expected_text.splitlines()
        actual = []
        for state in result.expect_states:
            actual.extend(state.matched_lines)

        width = 56
        rows = [
            "%-*s | %s" % (width, "EXPECTED (monitor)", "ACTUAL (matched lines)"),
            "%s-+-%s" % ("-" * width, "-" * width),
        ]
        for index in range(max(len(expected), len(actual))):
            left = expected[index] if index < len(expected) else ""
            right = actual[index] if index < len(actual) else ""
            rows.append("%-*s | %s" % (width, left[:width], right))

        rows.append("")
        rows.append("assertions:")
        rows.extend(self._summary_rows(self._last_specs, result.expect_states))
        if result.reasons:
            rows.append("")
            rows.append("reasons:")
            rows.extend("  - %s" % reason for reason in result.reasons)
        return "\n".join(rows) + "\n"

    def _render_result(self, result: RunResult) -> str:
        rows = [
            "scenario: %s" % result.scenario_name,
            "status: %s" % result.status,
            "real_elapsed_s: %.1f" % result.real_elapsed_s,
            "fake_elapsed_s: %.0f" % result.fake_elapsed_s,
        ]
        if result.error:
            rows.append("error: %s" % result.error)
        if result.reasons:
            rows.append("reasons:")
            rows.extend("  - %s" % reason for reason in result.reasons)
        rows.append("")
        rows.append("assertions:")
        rows.extend(self._summary_rows(self._last_specs, result.expect_states))
        return "\n".join(rows) + "\n"
