# Meter Payload Analysis - Decoded Fields

## Overview

This document details the analysis of the response payload returned by the Itron EverBlu Cyble Enhanced water meter via the RADIAN protocol.

The frame length byte advertises **124 bytes** (`0x7C`), but the firmware's 4x-oversampled decoder currently recovers only the first **120 bytes**: the final 4 bytes (the most recent history month and the CRC trailer) are lost to tail truncation. See [Frame truncation](#frame-truncation-124-vs-120-bytes) below.

The byte offsets in the table are confirmed against two independent sources:

- The RADIAN reference implementation `display_meter_report()` from the (archived) radianprotocol.com sources, which prints volume, meter clock, battery, wake/sleep hours, read counter and the identifier string from fixed offsets.
- The captured `home_001` fixture in [test/fixtures/meter_frames/fixtures.lst](../test/fixtures/meter_frames/fixtures.lst), whose every byte is reproduced below.

## Complete Payload Map

Example column values are the verified `home_001` capture (volume 768,837 L; meter clock 27/04/2026 09:59:49).

| Bytes    | Hex Value (home_001) | Status | Description |
|----------|----------------------|--------|-------------|
| [0]      | 7C                  | ✓ Known | Frame length byte (`0x7C` = 124, includes length + CRC) |
| [1]      | 11                  | ✓ Known | Control byte (`0x11` = Response) |
| [2]      | 00                  | ✓ Known | Spacer (`0x00`) |
| [3-7]    | 45 20 0A 50 14      | ✓ Known | Receiver address (reader/master) |
| [8]      | 00                  | ✓ Known | Spacer (`0x00`) |
| [9-13]   | 45 14 03 EE D6      | ✓ Known | Sender address (meter): `0x45`, year `0x14`=20, serial `03EED6`=257238 |
| [14]     | 00                  | ✓ Known | Spacer (`0x00`) |
| [15-17]  | 01 08 00            | ? Unknown | Command/status header |
| [18-21]  | 45 BB 0B 00         | ✓ **DECODED** | **Current volume (litres)** - uint32 LSB first<br>`0x000BBB45` = 768,837 litres |
| [22-23]  | 40 06               | ? Unknown | Unknown |
| [24]     | 1B                  | ✓ **DECODED** | **Meter clock - day** (`0x1B` = 27) |
| [25]     | 04                  | ✓ **DECODED** | **Meter clock - month** (`0x04` = April) |
| [26]     | 1A                  | ✓ **DECODED** | **Meter clock - year** (`0x1A` = 26 → 2026) |
| [27]     | 01                  | ? Unknown | Unknown (skipped by the reference) |
| [28]     | 09                  | ✓ **DECODED** | **Meter clock - hour** (`0x09` = 09) |
| [29]     | 3B                  | ✓ **DECODED** | **Meter clock - minute** (`0x3B` = 59) |
| [30]     | 31                  | ✓ **DECODED** | **Meter clock - second** (`0x31` = 49) |
| [31]     | 5F                  | ✓ **DECODED** | **Battery remaining (months)** (`0x5F` = 95) |
| [32-42]  | 31 33 33 32 39...   | ✓ **DECODED** | **Meter type / identifier (ASCII)** = "133290AL02" (NUL-terminated) |
| [43]     | 00                  | ? Unknown | Unknown / string terminator |
| [44]     | 06                  | ✓ **DECODED** | **Wake hour (24h)** (`0x06` = 06:00) |
| [45]     | 12                  | ✓ **DECODED** | **Sleep hour (24h)** (`0x12` = 18:00) |
| [46-47]  | 04 01               | ? Unknown | Unknown |
| [48]     | D7                  | ✓ **DECODED** | **Read counter** (`0xD7` = 215) |
| [49]     | 01                  | ? Unknown | Unknown |
| [50-55]  | 00 00 00 00 00 00   | ? Unknown | Unknown (zero in this capture) |
| [56-68]  | 80 × 13             | ? Partial | 13-slot status/marker array (`0x80` = "no data" sentinel) |
| [69]     | 84                  | ? Unknown | Unknown (precedes the history block) |
| [70-73]  | CB 9D 09 00         | ✓ **DECODED** | **Historical volume - oldest month** = 630,219 L |
| [74-77]  | 36 C4 09 00         | ✓ **DECODED** | Historical volume = 640,054 L |
| [78-81]  | F5 F5 09 00         | ✓ **DECODED** | Historical volume = 652,789 L |
| [82-85]  | 31 2F 0A 00         | ✓ **DECODED** | Historical volume = 667,441 L |
| [86-89]  | B6 70 0A 00         | ✓ **DECODED** | Historical volume = 684,214 L |
| [90-93]  | F5 B1 0A 00         | ✓ **DECODED** | Historical volume = 700,917 L |
| [94-97]  | 10 E0 0A 00         | ✓ **DECODED** | Historical volume = 712,720 L |
| [98-101] | 8D 02 0B 00         | ✓ **DECODED** | Historical volume = 721,549 L |
| [102-105]| 04 1F 0B 00         | ✓ **DECODED** | Historical volume = 728,836 L |
| [106-109]| BD 3E 0B 00         | ✓ **DECODED** | Historical volume = 736,957 L |
| [110-113]| FF 5D 0B 00         | ✓ **DECODED** | Historical volume = 744,959 L |
| [114-117]| 9A 79 0B 00         | ✓ **DECODED** | Historical volume - 12th month = 752,026 L |
| [118-121]| 07 97 ?? ??         | ⚠ Truncated | **13th (most recent) history month** - only `07 97` survives decode |
| [122-123]| ?? ??               | ⚠ Truncated | **CRC-16/KERMIT trailer** - lost to decoder tail truncation |

## Successfully Decoded Fields

### Current reading and status

1. **Current Volume** [18-21] - Total litres since installation (uint32, LSB first)
2. **Meter real-time clock** [24-26, 28-30] - Day / month / year (20xx) / hour / minute / second
3. **Battery Remaining** [31] - Months until battery replacement
4. **Meter type / identifier** [32-42] - ASCII string (e.g. "133290AL02")
5. **Wake Hour** [44] / **Sleep Hour** [45] - Daily wake window (0-23)
6. **Read Counter** [48] - Number of times the meter has been interrogated

### Historical data

- **12 complete monthly volumes** [70-117] - Total volume at the end of each month, uint32 LSB first, oldest first. A 13th (most recent) month lives at [118-121] but is currently lost to truncation.

## Frame truncation (124 vs 120 bytes)

The meter advertises a 124-byte frame (`0x7C`), structured as:

- history month #13 at bytes [118-121]
- CRC-16/KERMIT trailer at bytes [122-123]

The firmware decoder recovers only 120 bytes, so **both** of these are lost. Two consequences follow:

1. **The most recent history month is missing.** The history block is 13 monthly `uint32` values starting at byte 70; only 12 fit in the truncated buffer (`70 + 12 × 4 = 118`).
2. **The CRC is never actually validated on real meter frames.** Because the length byte (124) exceeds the decoded size (120), `radian_validate_crc()` takes its compatibility branch and returns `true` without checking. The surviving bytes `07 97` at [118-119] are history data, not the CRC (`crc_kermit(payload)` does not match them). This compatibility shim is therefore load-bearing: it must not be made strict until the decoder captures the full 124-byte frame, otherwise every real reading would be rejected.

The proper fix is in the RX/decoder path (capturing the final 4 bytes), not in the parser. It cannot be validated from an already-truncated fixture, so it is documented here rather than changed speculatively.

## Unknown/Undecoded Fields

### Header / control / status bytes [15-17], [22-23], [27], [43], [46-47], [49-55], [56-69]

Candidate meanings (from the AnyQuest / EverBlu Cyble Enhanced field catalogue) include preset billing-date indexes, time-of-use indexes, flow-threshold volumes, meter-sizing indicators, leak/backflow/tamper alarms, and peak-flow records. The 13-slot `0x80` run at [56-68] is consistent with a per-month status/marker array, but confirming any of these needs captures under varying meter states (alarms active, different meter models). Without official Itron documentation, mapping them requires:

1. Capturing many meter responses under varying conditions
2. Comparing payloads from multiple meter models
3. Analysing bit patterns for flags/counters
4. Triggering meter alarm conditions to observe status-flag changes

## Data Not Available via Basic RADIAN Protocol

The following information is **stored internally** in the meter but **NOT transmitted** in the basic RADIAN query response:

### Timestamps

- **Exact dates** for the 13 historical readings (meter knows them, doesn't send them)
- **Meter's current time** (internal RTC not queryable)
- **Timestamps for alarm events**
- **Peak flow occurrence dates**

### Enhanced Historical Data

- **181 consumption intervals** (hourly/daily/weekly/monthly) - only 13 monthly values available
- **Peak flow rates** with dates (up to 5 values)
- **Alarm logs** with start/end times (13 months of alarms)
- **Time-of-Use indexes** (multi-tariff billing support)
- **Backflow/reverse flow history**
- **Leak detection events**
- **Tamper detection alarms**

### Configuration Data

- **Meter sizing** (pipe diameter, flow capacity)
- **Preset billing dates** (up to 4 programmable dates)
- **Custom alarm thresholds**
- **Firmware version**

These enhanced features require **proprietary Itron commands** that are not documented in open-source RADIAN implementations.

## Comparison with Reference Implementation

The hallard/everblu-meters-pi Raspberry Pi reference implementation decodes the exact same fields:

- Current volume (liters)
- Battery remaining (months)
- Read counter
- Wake/sleep hours (time_start/time_end)
- RSSI, LQI (signal quality)

**It does NOT decode the historical data** - this is a new feature in this implementation.

## Decoding Methodology

### Integer Values (LSB First)

All multi-byte integers use **little-endian (LSB first)** encoding:

```
Bytes: [low, medium-low, medium-high, high]
Value = byte[0] + (byte[1] << 8) + (byte[2] << 16) + (byte[3] << 24)



Example: EE 01 0B 00
= 0xEE + (0x01 << 8) + (0x0B << 16) + (0x00 << 24)
= 238 + 256 + 720,896 + 0
= 721,390
```

### ASCII Text

Bytes [32-42] contain null-terminated ASCII text:

```
31 33 33 32 39 30 41 4C 30 32 00
= "13329 0AL02\0"
```

### Reserved Fields

The value **0x80** (128 decimal) appears to be used as a "no data" or "unused field" marker:

```
Bytes [54-65]: 00 00 80 80 80 80 80 80 80 80 80 80

               └─┘ └────────────────────────────┘
              Valid      Reserved/unused
```

## Implementation Status

✅ **Fully Implemented and Tested**

- Current volume extraction
- Battery remaining
- Read counter
- Wake/sleep schedule
- 13 months historical volumes (NEW)
- JSON attributes for Home Assistant (NEW)

⚠️ **Identified But Not Implemented**

- Meter model/serial ASCII extraction (bytes 32-42)
- Could be added as a diagnostic sensor if needed

❌ **Unknown/Not Attempted**

- Header/control bytes interpretation
- Status flags decoding

- Missing 2 bytes (118-121) investigation

## Recommendations

### For End Users

The newly decoded **13 months of historical data** provides significant value:

- Track consumption trends over a full year
- Detect seasonal patterns (higher summer usage)
- Identify anomalies (sudden increases indicating leaks)
- Calculate average monthly consumption
- Monitor current month progress vs. historical average

### For Future Development

1. **Meter Model Extraction**: Parse bytes 32-42 to create a device identifier sensor

2. **Status Flags**: Analyze unknown bytes for error/alarm flags
3. **Protocol Documentation**: Contact Itron or reverse engineer enhanced protocol for:
   - Actual timestamps for historical readings
   - Access to all 181 consumption intervals
   - Peak flow rates with dates
   - Alarm history

### For Researchers

The unknown bytes could be analyzed by:

1. Creating test cases with meters in different states
2. Comparing payloads from multiple meter models/years
3. Monitoring byte changes over time (counters, sequence numbers)
4. Testing edge cases (low battery, alarms, maximum volume)

## References

- **Payload Size**: 122 bytes (decoded), 124 bytes (frame length indicator)
- **Protocol**: RADIAN (Itron proprietary, basis for Wireless M-Bus Mode F)
- **Meter Model**: Itron EverBlu Cyble Enhanced (e.g., 2020-0123456)
- **Reference Implementation**: hallard/everblu-meters-pi (Raspberry Pi, C language)
- **This Implementation**: genestealer/everblu-meters-esp8266-improved (ESP8266, Arduino C++)

## Changelog

**October 30, 2025** - Historical Data Feature

- ✅ Decoded bytes [66-117] as 13 months of historical volume readings
- ✅ Added `history[13]` and `history_available` fields to `tmeter_data` structure
- ✅ Implemented automatic extraction in `parse_meter_report()`
- ✅ Created JSON attributes message with historical data + monthly usage calculations
- ✅ Integrated with Home Assistant MQTT discovery (json_attr_t)
- ✅ Documented complete payload structure and unknown fields
