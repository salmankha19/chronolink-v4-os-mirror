#!/usr/bin/env python3
"""device_controller.py -- serial + HTTP control surface for one device.

One DeviceController talks to one physical ChronoLink board over two channels:

  * USB serial -- Console log capture and the DTR/RTS hard reset that esptool
    uses (the only way to reboot without re-flashing).
  * HTTP on the LAN -- The test-only `/debug/ntp` hook (compiled in only for
    `env:single_mcu_test`, see CHRONOLINK_TEST_HOOKS in platformio.ini) which
    points the device's SNTP client at a mock server.

No scenario knowledge lives here: this module exposes primitives, commit 4's
scenario_runner.py sequences them.

Typical use:

    ctrl = DeviceController("COM7", esp_ip="192.168.68.70")
    ctrl.connect()
    ctrl.reset()
    for line in ctrl.read_lines(5):
        print(line)
    ctrl.set_ntp_host(detect_pc_lan_ip())
    ctrl.close()

Serial exceptions from connect() are deliberately not wrapped -- a wrong COM
port must surface as pyserial's SerialException, not as a generic RuntimeError.
"""

from __future__ import annotations

import json
import logging
import socket
import time
from typing import Iterator, Optional

import requests
import serial

LOG = logging.getLogger(__name__)

# Short read timeout so read_lines() can notice close()/deadline between reads
# without busy-waiting.
SERIAL_READ_TIMEOUT_S = 0.1

# Boot-to-HTTP-up is not instant, so a 404 means "not booted yet", not "wrong
# firmware". Three retries, 1 s / 2 s / 4 s, then give up.
NTP_404_RETRY_DELAYS_S = (1.0, 2.0, 4.0)

# One extra attempt when the first connect after 'Got IP' is refused: lwIP can
# take a moment to start accepting on a freshly configured interface.
CONNECTION_RETRY_DELAY_S = 1.0


def _reports_busy_sync(text: str) -> bool:
    """True for svc_http's 500 {"reason":"sync_failed","detail":"ESP_ERR_INVALID_STATE"}.

    That body is svc_time saying its automatic post-DHCP sync still holds
    s_sync_lock: it clears by itself, so the same GET retried a moment later
    succeeds. Its other 500s (nvs_unavailable, set_ntp_server_failed) are
    terminal. Best effort by design -- an unparseable body is "not that".
    """
    try:
        payload = json.loads(text)
    except (TypeError, ValueError):
        return False
    if not isinstance(payload, dict):
        return False
    return (
        payload.get("reason") == "sync_failed"
        and payload.get("detail") == "ESP_ERR_INVALID_STATE"
    )


class DeviceController:
    """Serial console + HTTP control for a single ChronoLink board."""

    def __init__(
        self, port: str, baud: int = 115200, esp_ip: Optional[str] = None
    ) -> None:
        self.port = port
        self.baud = baud
        self.esp_ip = esp_ip
        self._serial: Optional[serial.Serial] = None
        self._closed = True

    # -- lifecycle ---------------------------------------------------------

    def connect(self) -> None:
        """Open the serial port and drop anything already buffered."""
        if self._serial is not None:
            self.close()
        self._serial = serial.Serial(
            self.port, self.baud, timeout=SERIAL_READ_TIMEOUT_S
        )
        self._serial.reset_input_buffer()
        self._closed = False
        LOG.info("opened %s at %d baud", self.port, self.baud)

    def close(self) -> None:
        """Stop read_lines() and release the port. Safe to call twice."""
        self._closed = True
        if self._serial is not None:
            try:
                self._serial.close()
            except (serial.SerialException, OSError) as exc:
                LOG.warning("serial close failed: %s", exc)
            finally:
                self._serial = None
                LOG.info("closed %s", self.port)

    def reset(self) -> None:
        """Hard reset the board with the esptool DTR/RTS pulse.

        DTR drives GPIO0 and RTS drives EN on the DevKit-style auto-reset
        circuit: hold EN low with GPIO0 high, then release EN. That is a
        normal boot (not download mode), which is what a test run wants.
        """
        if self._serial is None:
            raise RuntimeError("not connected -- call connect() first")

        self._serial.setDTR(False)      # GPIO0 high -> normal boot
        self._serial.setRTS(True)       # EN low    -> hold in reset
        time.sleep(0.1)
        self._serial.setRTS(False)      # EN high   -> chip starts
        time.sleep(0.05)
        self._serial.reset_input_buffer()   # discard the glitchy boot noise
        LOG.info("device %s reset via DTR/RTS", self.port)

    # -- HTTP: NTP injection ----------------------------------------------

    def set_ntp_host(self, host: str, timeout_s: float = 15.0) -> None:
        """Point the device's SNTP client at `host` via `/debug/ntp`.

        timeout_s defaults to 15.0 because the handler blocks inside
        svc_time_sync_ntp_once(), which waits up to SVC_TIME_SYNC_WAIT_MS
        (10 s) for the sync to land. Anything <= 10 s times out mid-sync and
        leaves the device's NTP server persisted-but-unsynced.

        Retries 3 times (1 s / 2 s / 4 s) on HTTP 404, which for this firmware
        means the HTTP server is up but the route is not -- either still
        booting the app or a build without CHRONOLINK_TEST_HOOKS.

        Retries once on ConnectionError: a refused connection right after the
        board logs 'Got IP' is the lwIP accept-lag race, not a dead board.

        Retries the 500 that svc_http reports as sync_failed /
        ESP_ERR_INVALID_STATE, sharing the 404 backoff: that one means
        svc_time's automatic post-DHCP sync still holds its lock and clears on
        its own. Defence in depth behind ScenarioRunner._wait_for_auto_sync().

        Everything else is terminal: a read timeout, and the HTTP errors the
        board means (500 nvs_unavailable, 500 set_ntp_server_failed, 400
        bad_host). Retrying those would just repeat the answer.
        """
        if self.esp_ip is None:
            raise RuntimeError(
                "esp_ip not set. Pass esp_ip='192.168.x.x' to DeviceController()"
            )

        url = f"http://{self.esp_ip}/debug/ntp?host={host}"
        delays = list(NTP_404_RETRY_DELAYS_S)
        connect_retried = False

        while True:
            try:
                response = requests.get(url, timeout=timeout_s)
            except requests.exceptions.ConnectionError as exc:
                if connect_retried:
                    raise RuntimeError(f"GET {url} failed: {exc}") from exc
                connect_retried = True
                LOG.warning(
                    "GET %s -> connection refused, retrying once in %.1f s",
                    url,
                    CONNECTION_RETRY_DELAY_S,
                )
                time.sleep(CONNECTION_RETRY_DELAY_S)
                continue
            except requests.exceptions.RequestException as exc:
                # Read timeout / DNS / TLS: retrying inside this call would
                # just repeat the same failure.
                raise RuntimeError(f"GET {url} failed: {exc}") from exc

            if response.status_code == 404 and delays:
                delay = delays.pop(0)
                LOG.warning(
                    "GET %s -> 404 (device still booting?), retrying in %.0f s",
                    url,
                    delay,
                )
                time.sleep(delay)
                continue

            if response.status_code == 500 and delays and _reports_busy_sync(
                response.text
            ):
                # Same backoff pool as the 404 retries, so a stuck board still
                # costs at most four GETs.
                delay = delays.pop(0)
                LOG.warning(
                    "GET %s -> 500 sync_failed (auto-sync still running?), "
                    "retrying in %.0f s",
                    url,
                    delay,
                )
                time.sleep(delay)
                continue

            if response.status_code != 200:
                raise RuntimeError(
                    f"GET {url} -> HTTP {response.status_code}: "
                    f"{response.text[:200]}"
                )

            LOG.info(
                "NTP host set: %s (device said: %s)", host, response.text[:100]
            )
            return

    def restore_ntp_host(self, default_host: str = "time.google.com",
                         timeout_s: float = 12.0) -> bool:
        """Single-shot NVS slot 0 restore. No retry, no backoff.

        Returns True on 200 OK OR 500 sync_failed (both imply NVS was
        written; sync was just refused by the 60 s resync floor).
        Returns False on any other error. Never raises.

        timeout_s is 12 s because the endpoint's follow-up sync
        (svc_time_sync_ntp_once) blocks up to 10 s, and the last sync is
        normally >60 s old by the time a scenario ends -- so it does run.
        A shorter timeout would fire on a restore that actually succeeded.

        Called from ScenarioRunner.run()'s finally block, which is why it
        must not retry and must not raise: set_ntp_host()'s 1 s / 2 s / 4 s
        backoff would turn an already-restored board into 7 s of delay and a
        false alarm. The 500-sync_failed case is the reason this cannot just
        call set_ntp_host().

        The test hook persists whatever host it is given, so a board left
        alone keeps syncing from this PC on every later boot -- including
        boots of production firmware, which has no /debug/ntp to undo it.
        This is the undo.
        """
        if self.esp_ip is None:
            return False

        url = f"http://{self.esp_ip}/debug/ntp?host={default_host}"
        try:
            response = requests.get(url, timeout=timeout_s)
        except Exception as exc:      # noqa: BLE001 -- never raise from cleanup
            # Deliberately broader than RequestException: the contract is
            # "returns a bool, raises nothing", and this call site is a
            # single place where a surprise exception would be least
            # welcome.
            LOG.warning("restore NTP host failed: %s", exc)
            return False

        if response.status_code == 200:
            LOG.info(
                "NTP host restored to %s (device said: %s)",
                default_host,
                response.text[:100],
            )
            return True

        if response.status_code == 500 and _reports_busy_sync(response.text):
            # The host is already in NVS; only the follow-up sync was
            # skipped because svc_time's automatic one still held the lock.
            LOG.info(
                "NTP host restored to %s (follow-up sync skipped)",
                default_host,
            )
            return True

        LOG.warning(
            "restore NTP host failed: HTTP %d: %s",
            response.status_code,
            response.text[:200],
        )
        return False

    # -- serial console ----------------------------------------------------

    def read_lines(self, timeout_s: Optional[float] = None) -> Iterator[str]:
        """Yield console lines as they arrive.

        timeout_s=None reads until close() is called; a number bounds the
        whole iteration wall-clock. Partial lines (a read that timed out
        mid-line) are yielded as-is rather than buffered, which is enough for
        the firmware's newline-terminated log output.
        """
        if self._serial is None:
            raise RuntimeError("not connected -- call connect() first")

        deadline = None if timeout_s is None else time.monotonic() + timeout_s

        while not self._closed and self._serial is not None:
            if deadline is not None and time.monotonic() >= deadline:
                return
            try:
                raw = self._serial.readline()
            except (serial.SerialException, OSError) as exc:
                LOG.warning("serial read failed: %s", exc)
                return
            if not raw:
                continue        # read timeout, not EOF -- keep polling
            yield raw.decode("utf-8", errors="replace").rstrip("\r\n")


def detect_pc_lan_ip() -> str:
    """Return a non-loopback IPv4 address, or raise RuntimeError.

    A UDP socket connect() sends no packets; it just makes the OS pick the
    interface it would route through, which is the address the device on the
    same LAN can reach.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        ip = sock.getsockname()[0]
    except OSError as exc:
        raise RuntimeError("no LAN IP available") from exc
    finally:
        sock.close()

    if not ip or ip.startswith("127.") or ip.startswith("169.254."):
        raise RuntimeError("no LAN IP available")
    return ip
