#!/usr/bin/env python3
"""Upload to or monitor both ESP32 boards selected by their hardware MAC."""

from __future__ import annotations

import argparse
import glob
import re
import signal
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[1]
MAC_RE = re.compile(r"(?:MAC|WiFi MAC|Ethernet MAC)\s*:\s*([0-9A-Fa-f:]{17})")


def normalize_mac(value: str) -> str:
    compact = re.sub(r"[^0-9A-Fa-f]", "", value)
    if len(compact) != 12:
        raise argparse.ArgumentTypeError(f"invalid ESP32 MAC address: {value}")
    return compact.lower()


def candidate_ports() -> list[str]:
    paths = glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*")
    return sorted(set(paths))


def detect_mac(port: str) -> str | None:
    commands = [
        ["python3", "-m", "esptool", "--chip", "esp32", "--port", port, "chip_id"],
        ["esptool.py", "--chip", "esp32", "--port", port, "chip_id"],
        ["pio", "pkg", "exec", "--package", "tool-esptoolpy", "--", "esptool.py", "--chip", "esp32", "--port", port, "chip_id"],
    ]
    for command in commands:
        try:
            result = subprocess.run(
                command,
                cwd=PROJECT_DIR,
                capture_output=True,
                text=True,
                timeout=15,
                check=False,
            )
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
        output = f"{result.stdout}\n{result.stderr}"
        match = MAC_RE.search(output)
        if match:
            return normalize_mac(match.group(1))
    return None


def command_for(mode: str, project: str, port: str) -> list[str]:
    command = ["pio", "run", "-d", project]
    if mode == "upload":
        command += ["-t", "upload"]
    else:
        command += ["-t", "monitor"]
    return command + ["--upload-port", port] if mode == "upload" else command + ["--port", port]


def run_parallel(commands: list[tuple[str, list[str]]]) -> int:
    processes = []
    for label, command in commands:
        print(f"[{label}] {' '.join(command)}", flush=True)
        processes.append((label, subprocess.Popen(command, cwd=PROJECT_DIR)))

    try:
        return_codes = [process.wait() for _, process in processes]
    except KeyboardInterrupt:
        print("\nStopping both processes...", flush=True)
        for _, process in processes:
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
        return_codes = [process.wait() for _, process in processes]

    for (label, _), return_code in zip(processes, return_codes):
        print(f"[{label}] exited with code {return_code}", flush=True)
    return max(return_codes, default=0)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("list", "upload", "monitor"))
    parser.add_argument("--control-mac", type=normalize_mac)
    parser.add_argument("--ui-mac", type=normalize_mac)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    ports = candidate_ports()
    if len(ports) < 2:
        print(f"Found {len(ports)} serial port(s); two ESP32 ports are required.", file=sys.stderr)
        return 2

    print("Detecting ESP32 MAC addresses...", flush=True)
    detected = {port: detect_mac(port) for port in ports}
    if args.mode == "list":
        for port, mac in detected.items():
            print(f"{port}: {mac or 'unavailable'}")
        return 0

    if not args.control_mac or not args.ui_mac:
        print("upload and monitor require both --control-mac and --ui-mac.", file=sys.stderr)
        return 2

    control_matches = [port for port, mac in detected.items() if mac == args.control_mac]
    ui_matches = [port for port, mac in detected.items() if mac == args.ui_mac]
    if not control_matches:
        print(f"No connected ESP32 with MAC {args.control_mac}.", file=sys.stderr)
        return 2
    if not ui_matches:
        print(f"No connected ESP32 with MAC {args.ui_mac}.", file=sys.stderr)
        return 2
    if len(control_matches) > 1 or len(ui_matches) > 1:
        print("A MAC address matched multiple serial ports.", file=sys.stderr)
        return 2
    control_port = control_matches[0]
    ui_port = ui_matches[0]
    if control_port == ui_port:
        print("Control and UI MACs resolved to the same port.", file=sys.stderr)
        return 2

    print(f"Control {args.control_mac} -> {control_port}")
    print(f"UI      {args.ui_mac} -> {ui_port}")
    return run_parallel([
        ("control", command_for(args.mode, "control-esp", control_port)),
        ("ui", command_for(args.mode, "ui-esp", ui_port)),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
