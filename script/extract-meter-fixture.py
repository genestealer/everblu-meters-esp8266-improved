#!/usr/bin/env python3
"""
Extract decoded meter frame dumps from firmware logs and generate fixture lines.

Works with logs from BOTH MQTT standalone firmware and ESPHome builds.

MQTT standalone (raw Serial) format:
  [CC1101] Full hex dump of decoded frame (122 bytes):
  [000-015]: 7C 44 2D ...
  [016-031]: ...

ESPHome logger format (with component prefix):
  [I][everblu_meter:123]: [CC1101] Full hex dump of decoded frame (122 bytes):
  [I][everblu_meter:124]: [000-015]: 7C 44 2D ...
  [D][everblu_meter:125]: [016-031]: ...

Collect ESPHome logs with: esphome logs your-device.yaml > logs_water_meter_logs.txt
Collect MQTT logs with:    pio device monitor --baud 115200 | Tee-Object capture.log
"""

from __future__ import annotations

import argparse
import pathlib
import re
from dataclasses import dataclass
from typing import List

# Matches hex dump offset lines in both formats:
#   MQTT:     [000-015]: 7C 44 ...
#   ESPHome:  [10:53:27.465][I][everblu_meter:123]: [000-015]: 7C 44 ...
HEX_LINE_RE = re.compile(r"\[(\d+)-(\d+)\]:\s+(.*)$")
HEX_BYTE_RE = re.compile(r"\b[0-9A-Fa-f]{2}\b")

# ESPHome logger format: [timestamp][level][component:line]: message
# Strip all leading [...][ groups to expose the bare message.
_ESPHOME_PREFIX_RE = re.compile(r"^(?:\[[^\]]*\]\s*)+:\s*")

def _strip_esphome_prefix(line: str) -> str:
    """Remove an ESPHome-style log prefix while preserving raw MQTT dump lines."""
    if HEX_LINE_RE.match(line):
        return line
    m = _ESPHOME_PREFIX_RE.match(line)
    return line[m.end():] if m else line


def crc_kermit(data: bytes) -> int:
    crc = 0x0000
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    low = (crc & 0xFF00) >> 8
    high = (crc & 0x00FF) << 8
    return (low | high) & 0xFFFF


@dataclass
class ParsedFrame:
    name: str
    decoded: bytes
    volume: int
    battery: int
    counter: int
    time_start: int
    time_end: int
    history_available: int
    crc_valid: int


def parse_fields(decoded: bytes) -> tuple[int, int, int, int, int, int]:
    volume = 0
    battery = 0
    counter = 0
    time_start = 0
    time_end = 0
    history_available = 0

    if len(decoded) >= 22:
        volume = decoded[18] | (decoded[19] << 8) | (decoded[20] << 16) | (decoded[21] << 24)

    if len(decoded) >= 49:
        battery = decoded[31]
        counter = decoded[48]
        time_start = decoded[44]
        time_end = decoded[45]

    if len(decoded) >= 118:
        history_available = 1

    return volume, battery, counter, time_start, time_end, history_available


def validate_crc(decoded: bytes) -> int:
    if len(decoded) < 4:
        return 0

    expected_len = decoded[0] if decoded[0] else len(decoded)

    if expected_len > len(decoded):
        missing = expected_len - len(decoded)
        if missing == 2:
            return 1
        return 1

    if expected_len < 4:
        return 0

    crc_offset = expected_len - 2
    if crc_offset + 1 >= len(decoded):
        return 1

    received = (decoded[crc_offset] << 8) | decoded[crc_offset + 1]
    computed = crc_kermit(decoded[1 : expected_len - 2])
    return 1 if computed == received else 0


def collect_frames(log_text: str, prefix: str) -> List[ParsedFrame]:
    lines = log_text.splitlines()
    frames: List[ParsedFrame] = []
    i = 0
    idx = 1

    while i < len(lines):
        if "Full hex dump of decoded frame" not in lines[i]:
            i += 1
            continue

        i += 1
        bytes_out: List[int] = []
        while i < len(lines):
            stripped = _strip_esphome_prefix(lines[i].strip())
            m = HEX_LINE_RE.match(stripped)
            if not m:
                # Skip header/separator lines before the first hex row (e.g. "Offset : Hex Data")
                if not bytes_out:
                    i += 1
                    continue
                break
            hex_part = m.group(3)
            for hb in HEX_BYTE_RE.findall(hex_part):
                bytes_out.append(int(hb, 16))
            i += 1

        if bytes_out:
            decoded = bytes(bytes_out)
            volume, battery, counter, time_start, time_end, history_available = parse_fields(decoded)
            crc_valid = validate_crc(decoded)
            name = f"{prefix}_{idx:03d}"
            frames.append(
                ParsedFrame(
                    name=name,
                    decoded=decoded,
                    volume=volume,
                    battery=battery,
                    counter=counter,
                    time_start=time_start,
                    time_end=time_end,
                    history_available=history_available,
                    crc_valid=crc_valid,
                )
            )
            idx += 1

    return frames


def to_fixture_line(frame: ParsedFrame) -> str:
    decoded_hex = " ".join(f"{b:02X}" for b in frame.decoded)
    return (
        f"{frame.name}|{decoded_hex}|{frame.volume}|{frame.battery}|{frame.counter}|"
        f"{frame.time_start}|{frame.time_end}|{frame.history_available}|{frame.crc_valid}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract meter fixtures from firmware logs")
    parser.add_argument(
        "--input",
        required=True,
        help="Path to firmware log file (.log or .txt)",
    )
    parser.add_argument(
        "--output",
        default="test/fixtures/meter_frames/fixtures.lst",
        help="Fixture list output path",
    )
    parser.add_argument(
        "--name-prefix",
        default="capture",
        help="Prefix used for generated fixture names",
    )
    parser.add_argument(
        "--append",
        action="store_true",
        help="Append to existing fixture list instead of overwriting",
    )
    args = parser.parse_args()

    input_path = pathlib.Path(args.input)
    if not input_path.exists():
        raise SystemExit(f"Input log file not found: {input_path}")

    # Accept both serial capture files and ESPHome web-exported text logs.
    allowed_suffixes = {".log", ".txt"}
    suffix = input_path.suffix.lower()
    if suffix and suffix not in allowed_suffixes:
        raise SystemExit(
            f"Unsupported input extension '{suffix}'. Use a .log or .txt file."
        )

    log_text = input_path.read_text(encoding="utf-8", errors="ignore")
    frames = collect_frames(log_text, args.name_prefix)
    if not frames:
        raise SystemExit("No decoded frame dumps found in input log")

    out_path = pathlib.Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    mode = "a" if args.append and out_path.exists() else "w"
    with out_path.open(mode, encoding="utf-8") as f:
        if mode == "w":
            f.write("# fixture_name|decoded_hex|volume|battery|counter|time_start|time_end|history_available|crc_valid\n")
        for frame in frames:
            f.write(to_fixture_line(frame) + "\n")

    print(f"Extracted {len(frames)} frame(s) into {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
