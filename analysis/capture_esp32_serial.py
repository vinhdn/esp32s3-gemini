#!/usr/bin/env python3
"""Read ESP32 USB serial without external pyserial dependency."""

from __future__ import annotations

import argparse
import datetime as dt
import os
import select
import sys
import termios
import time
import tty


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/cu.usbmodem1301")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration", type=float, default=30.0)
    args = parser.parse_args()

    baud_constant = getattr(termios, f"B{args.baud}", None)
    if baud_constant is None:
        parser.error(f"Unsupported baud rate: {args.baud}")

    fd = os.open(args.port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    original = termios.tcgetattr(fd)
    try:
        attrs = termios.tcgetattr(fd)
        tty.setraw(fd)
        attrs = termios.tcgetattr(fd)
        attrs[4] = baud_constant
        attrs[5] = baud_constant
        termios.tcsetattr(fd, termios.TCSANOW, attrs)

        deadline = time.monotonic() + args.duration
        pending = b""
        while time.monotonic() < deadline:
            readable, _, _ = select.select([fd], [], [], 0.25)
            if not readable:
                continue
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                # macOS can report EAGAIN even after select() marked a USB
                # serial descriptor readable; treat it as no data yet.
                continue
            if not chunk:
                continue
            pending += chunk
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                timestamp = dt.datetime.now(dt.timezone.utc).isoformat()
                text = line.rstrip(b"\r").decode("utf-8", errors="replace")
                print(f"[ESP32] {timestamp} text={text!r} hex={line.hex()}", flush=True)

        if pending:
            timestamp = dt.datetime.now(dt.timezone.utc).isoformat()
            text = pending.decode("utf-8", errors="replace")
            print(f"[ESP32] {timestamp} text={text!r} hex={pending.hex()}", flush=True)
    finally:
        termios.tcsetattr(fd, termios.TCSANOW, original)
        os.close(fd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
