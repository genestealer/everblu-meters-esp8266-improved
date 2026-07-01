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
from dataclasses import dataclass
import pathlib
import re

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
    return line[m.end() :] if m else line


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
        volume = (
            decoded[18] | (decoded[19] << 8) | (decoded[20] << 16) | (decoded[21] << 24)
        )

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

    expected_len = decoded[0] or len(decoded)

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


def collect_hex_blocks(log_text: str, header_substring: str) -> list[bytes]:
    """Collect all hex dump blocks whose header line contains header_substring.

    A block is the sequence of ``[NNN-NNN]: XX XX ...`` offset lines that follow a
    matching header line, in both raw-Serial and ESPHome logger formats.
    """
    lines = log_text.splitlines()
    blocks: list[bytes] = []
    i = 0
    while i < len(lines):
        if header_substring not in lines[i]:
            i += 1
            continue

        i += 1
        bytes_out: list[int] = []
        while i < len(lines):
            stripped = _strip_esphome_prefix(lines[i].strip())
            m = HEX_LINE_RE.match(stripped)
            if not m:
                # Skip header/separator lines before the first hex row.
                if not bytes_out:
                    i += 1
                    continue
                break
            bytes_out.extend(int(hb, 16) for hb in HEX_BYTE_RE.findall(m.group(3)))
            i += 1

        if bytes_out:
            blocks.append(bytes(bytes_out))

    return blocks


def collect_frames(log_text: str, prefix: str) -> list[ParsedFrame]:
    frames: list[ParsedFrame] = []
    for idx, decoded in enumerate(
        collect_hex_blocks(log_text, "Full hex dump of decoded frame"), start=1
    ):
        volume, battery, counter, time_start, time_end, history_available = (
            parse_fields(decoded)
        )
        frames.append(
            ParsedFrame(
                name=f"{prefix}_{idx:03d}",
                decoded=decoded,
                volume=volume,
                battery=battery,
                counter=counter,
                time_start=time_start,
                time_end=time_end,
                history_available=history_available,
                crc_valid=validate_crc(decoded),
            )
        )
    return frames


def to_fixture_line(frame: ParsedFrame) -> str:
    decoded_hex = " ".join(f"{b:02X}" for b in frame.decoded)
    return (
        f"{frame.name}|{decoded_hex}|{frame.volume}|{frame.battery}|{frame.counter}|"
        f"{frame.time_start}|{frame.time_end}|{frame.history_available}|{frame.crc_valid}"
    )


def collect_raw_captures(log_text: str, prefix: str) -> list[str]:
    """Pair each raw (pre-decode) RX dump with the decoded dump from the same read.

    Requires the firmware built with -DDUMP_RAW_RX_FRAME (raw block) and debug hex
    output enabled (decoded block, used only to fill the expected meter fields).
    """
    raw_blocks = collect_hex_blocks(log_text, "Full hex dump of RAW RX frame")
    decoded_blocks = collect_hex_blocks(log_text, "Full hex dump of decoded frame")
    if len(raw_blocks) != len(decoded_blocks):
        raise SystemExit(
            "Raw/decoded dump count mismatch "
            f"({len(raw_blocks)} raw vs {len(decoded_blocks)} decoded); "
            "capture with both -DDUMP_RAW_RX_FRAME and DEBUG_CC1101 enabled."
        )

    lines: list[str] = []
    for idx, (raw, decoded) in enumerate(
        zip(raw_blocks, decoded_blocks, strict=False), start=1
    ):
        volume, battery, counter, time_start, time_end, history_available = (
            parse_fields(decoded)
        )
        raw_hex = " ".join(f"{b:02X}" for b in raw)
        lines.append(
            f"{prefix}_{idx:03d}|{raw_hex}|{volume}|{battery}|{counter}|"
            f"{time_start}|{time_end}|{history_available}"
        )
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract meter fixtures from firmware logs"
    )
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
    parser.add_argument(
        "--raw",
        action="store_true",
        help=(
            "Extract REAL pre-decode raw RX captures (needs firmware built with "
            "-DDUMP_RAW_RX_FRAME) into raw_captures.lst for the native_hal simulation"
        ),
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

    if args.raw:
        raw_lines = collect_raw_captures(log_text, args.name_prefix)
        if not raw_lines:
            raise SystemExit(
                "No raw RX dumps found; build firmware with -DDUMP_RAW_RX_FRAME."
            )
        out_path = pathlib.Path(
            args.output
            if args.output != "test/fixtures/meter_frames/fixtures.lst"
            else "test/fixtures/meter_frames/raw_captures.lst"
        )
        out_path.parent.mkdir(parents=True, exist_ok=True)
        mode = "a" if args.append and out_path.exists() else "w"
        with out_path.open(mode, encoding="utf-8") as f:
            if mode == "w":
                f.write(
                    "# capture_name|raw_rx_hex|volume|battery|counter|time_start|time_end|history_available\n"
                )
            for line in raw_lines:
                f.write(line + "\n")
        print(f"Extracted {len(raw_lines)} raw capture(s) into {out_path}")
        return 0

    frames = collect_frames(log_text, args.name_prefix)
    if not frames:
        raise SystemExit("No decoded frame dumps found in input log")

    out_path = pathlib.Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    mode = "a" if args.append and out_path.exists() else "w"
    with out_path.open(mode, encoding="utf-8") as f:
        if mode == "w":
            f.write(
                "# fixture_name|decoded_hex|volume|battery|counter|time_start|time_end|history_available|crc_valid\n"
            )
        for frame in frames:
            f.write(to_fixture_line(frame) + "\n")

    print(f"Extracted {len(frames)} frame(s) into {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
