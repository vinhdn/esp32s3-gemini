#!/usr/bin/env python3
"""Attach Frida to VIETMAP Live and print H50 capture records.

Usage:
  python3 analysis/capture_vietmap_h50.py --device raman801df76c07c02d10 --spawn
  python3 analysis/capture_vietmap_h50.py --device <adb-serial>
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import threading
import typing

# frida 17.17 imports these names from typing, but Python 3.10 exposes them
# through typing_extensions instead.
try:
    from typing import NotRequired, Required  # type: ignore[attr-defined]
except ImportError:
    from typing_extensions import NotRequired, Required

    typing.NotRequired = NotRequired  # type: ignore[attr-defined]
    typing.Required = Required  # type: ignore[attr-defined]

import frida
import frida_tools

PACKAGE = "vn.vietmap.live"
SCRIPT_PATH = pathlib.Path(__file__).with_suffix(".js")
JAVA_BRIDGE_PATH = pathlib.Path(frida_tools.__file__).with_name("bridges") / "java.js"


def on_message(message: dict, data: bytes | None) -> None:
    message_type = message.get("type")
    if message_type == "send":
        print(message.get("payload"), flush=True)
    elif message_type == "log":
        print(message.get("payload"), flush=True)
    else:
        print(f"[frida:{message_type}] {message}", file=sys.stderr, flush=True)
    if data:
        print(f"[frida:data] {data.hex()}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", required=True, help="ADB/Frida device serial")
    parser.add_argument("--spawn", action="store_true", help="Spawn the app instead of attaching")
    parser.add_argument("--pid", type=int, help="Attach to an existing PID")
    parser.add_argument("--duration", type=float, help="Detach automatically after N seconds")
    parser.add_argument(
        "--relay-speed-limit",
        action="store_true",
        help="Send plaintext VMSL frames to the connected VIETMAP_HUD_H50",
    )
    parser.add_argument("--package", default=PACKAGE)
    args = parser.parse_args()

    relay_setting = "true" if args.relay_speed_limit else "false"
    source = (
        JAVA_BRIDGE_PATH.read_text(encoding="utf-8")
        + "\nglobalThis.Java = bridge;\n"
        + f"globalThis.VMH50_RELAY = {relay_setting};\n"
        + SCRIPT_PATH.read_text(encoding="utf-8")
    )
    device = frida.get_device(args.device, timeout=10)

    spawned_pid: int | None = None
    if args.spawn:
        spawned_pid = device.spawn([args.package])
        session = device.attach(spawned_pid)
    elif args.pid is not None:
        session = device.attach(args.pid)
    else:
        session = device.attach(args.package)

    script = session.create_script(source)
    script.on("message", on_message)
    script.load()

    if spawned_pid is not None:
        device.resume(spawned_pid)

    print(
        f"Attached to {args.package} on {args.device}. Press Ctrl-C to stop.",
        file=sys.stderr,
        flush=True,
    )
    try:
        if args.duration is None:
            threading.Event().wait()
        else:
            threading.Event().wait(args.duration)
    except KeyboardInterrupt:
        pass
    finally:
        session.detach()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
