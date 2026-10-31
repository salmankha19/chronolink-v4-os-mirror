#!/usr/bin/env python3
"""mock_ntp_server.py -- time-injection NTP server for the ChronoLink harness.

Serves a fake UTC clock over UDP so a scenario can drag the device to any
date, time or DST transition in seconds instead of waiting for real prayer
times. The reported time is

    fake_now = start_fake + (time.time() - start_real) * speed

so --speed 600 walks a day of device time in ~2.4 minutes.

Standalone by design: this module imports nothing from the rest of
test_harness/ and can be run on its own for manual poking:

    python test_harness/mock_ntp_server.py --start "2026-11-01 09:00:00" --speed 600

Windows note: UDP:123 belongs to the Windows Time service (W32Time). The
server dies with an actionable message when the bind fails (see AM9 in
docs/session5.7-prompt.md). The ESP32 SNTP client always targets port 123 on
the configured host, so --port is only useful for a laptop-side smoke test.

Protocol: standard 48-byte NTPv4 server reply -- LI=0, VN=4, Mode=4,
stratum 1, ref_id 'LOCL', and the request's transmit timestamp echoed back
as the response's originate timestamp.
"""

from __future__ import annotations

import argparse
import errno
import socket
import struct
import sys
import time
from datetime import datetime, timezone

# Seconds between the NTP epoch (1900-01-01) and the Unix epoch (1970-01-01).
NTP_EPOCH_OFFSET = 2208988800
NTP_PACKET_SIZE = 48
NTP_FRAC = 1 << 32

# Octet 0: LI=0 (no leap warning), VN=4, Mode=4 (server).
NTP_FLAGS = (0 << 6) | (4 << 3) | 4
NTP_STRATUM = 1
NTP_POLL = 4            # 16 s, what a real stratum-1 server advertises
NTP_PRECISION = -20     # ~1 us, signed
REF_ID = b"LOCL"        # RFC 5905: uncalibrated local clock
ROOT_DELAY = 0
ROOT_DISPERSION = 16 << 16   # 16/65536 s, in 16.16 fixed point

# Windows: WSAEACCES (port owned / privileged) and WSAEADDRINUSE (W32Time).
WINERROR_DENIED = 10013
WINERROR_IN_USE = 10048


def to_ntp(epoch: float) -> bytes:
    """Convert Unix epoch seconds (UTC) to a 64-bit NTP timestamp."""
    whole = int(epoch)
    fraction = int(round((epoch - whole) * NTP_FRAC))
    if fraction >= NTP_FRAC:          # rounding carry
        whole += 1
        fraction = 0
    return struct.pack("!II", (whole + NTP_EPOCH_OFFSET) & 0xFFFFFFFF, fraction)


def build_response(request: bytes, fake_epoch: float) -> bytes:
    """Build the 48-byte NTPv4 reply for one request packet."""
    stamp = to_ntp(fake_epoch)
    originate = request[40:48]        # echo the client's transmit timestamp
    header = struct.pack(
        "!BBbbII4s",
        NTP_FLAGS,
        NTP_STRATUM,
        NTP_POLL,
        NTP_PRECISION,
        ROOT_DELAY,
        ROOT_DISPERSION,
        REF_ID,
    )
    # header(16) + reference(8) + originate(8) + receive(8) + transmit(8)
    return header + stamp + originate + stamp + stamp


def parse_start(value: str) -> float:
    """argparse type for --start: UTC 'YYYY-MM-DD HH:MM:SS' -> epoch seconds."""
    try:
        stamp = datetime.strptime(value, "%Y-%m-%d %H:%M:%S")
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "expected 'YYYY-MM-DD HH:MM:SS' in UTC, got %r" % (value,)
        ) from exc
    return stamp.replace(tzinfo=timezone.utc).timestamp()


def is_port_unavailable(exc: OSError) -> bool:
    """True when a bind failure means 'someone else already owns the port'."""
    if isinstance(exc, PermissionError):
        return True
    if exc.errno in (errno.EACCES, errno.EADDRINUSE):
        return True
    return getattr(exc, "winerror", None) in (WINERROR_DENIED, WINERROR_IN_USE)


def report_bind_failure(port: int) -> None:
    """AM9 guidance: W32Time owns UDP:123 on Windows."""
    message = (
        "ERROR: cannot bind UDP:%d \u2014 W32Time or another service owns it.\n"
        "Fix (PowerShell as admin):\n"
        "  net stop w32time\n"
        "After testing:\n"
        "  net start w32time\n"
        "Or run with --port 12300 (but ESP32 SNTP client always uses 123)."
        % port
    )
    try:
        sys.stderr.write(message + "\n")
        sys.stderr.flush()
    except UnicodeEncodeError:
        # Legacy OEM code page: never let the hint itself be the crash.
        sys.stderr.write(message.encode("ascii", "replace").decode("ascii") + "\n")
        sys.stderr.flush()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        prog="mock_ntp_server.py",
        description=(
            "Mock NTP server with time acceleration. The fake UTC time is "
            "start_fake + (elapsed_real * speed), so one real second can walk "
            "a minute, an hour or a day of device time."
        ),
        epilog=(
            'example: python mock_ntp_server.py --start "2026-11-01 09:00:00" '
            "--speed 600"
        ),
    )
    parser.add_argument(
        "--start",
        required=True,
        type=parse_start,
        metavar='"YYYY-MM-DD HH:MM:SS"',
        help="fake UTC time the device sees when the server starts",
    )
    parser.add_argument(
        "--speed",
        type=float,
        default=1.0,
        help="time acceleration factor (default 1.0 = real time)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=123,
        help="UDP port to listen on (default 123, which the ESP32 always uses)",
    )
    args = parser.parse_args(argv)

    if args.speed <= 0.0:
        parser.error("--speed must be greater than 0")
    if not 0 < args.port < 65536:
        parser.error("--port must be between 1 and 65535")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # No SO_REUSEADDR on purpose: on Windows it lets a second socket steal
        # the port silently, which would hide the W32Time conflict AM9 is about.
        sock.bind(("0.0.0.0", args.port))
    except OSError as exc:
        sock.close()
        if is_port_unavailable(exc):
            report_bind_failure(args.port)
            return 1
        raise

    start_fake = args.start
    # The fake clock is anchored at the FIRST REQUEST, not at process start.
    # Boot time (sensor probes, DHCP, RTC read) varies by seconds between
    # runs, and the mock is clocked faster than real time, so anchoring at
    # process start made the landing point drift by speed x jitter: at 600x a
    # 2.5 s boot spread moved it 26 fake minutes, straight past the prayer the
    # scenario was aiming at. Anchoring at the first request makes "the device
    # boots at start_utc" exact and repeatable, which is what the scenarios and
    # expected_output/*.md pages claim.
    start_real = None
    served = 0
    print(
        "mock NTP on udp/%d -- fake %s UTC at %gx, anchored at first request "
        "(Ctrl+C to stop)"
        % (
            args.port,
            datetime.fromtimestamp(start_fake, timezone.utc).strftime(
                "%Y-%m-%d %H:%M:%S"
            ),
            args.speed,
        ),
        flush=True,
    )

    try:
        while True:
            request, peer = sock.recvfrom(1024)
            if len(request) < NTP_PACKET_SIZE:
                continue          # not an NTP packet; keep serving
            if start_real is None:
                start_real = time.time()   # first request == fake start_fake
            fake_now = start_fake + (time.time() - start_real) * args.speed
            sock.sendto(build_response(request, fake_now), peer)
            served += 1
            print("%s@%.3f" % (peer[0], fake_now), flush=True)
    except KeyboardInterrupt:
        print("\n%d request(s) served" % served, flush=True)
        return 0
    finally:
        sock.close()


if __name__ == "__main__":
    sys.exit(main())
