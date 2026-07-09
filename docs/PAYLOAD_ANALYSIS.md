# Meter Payload Analysis - Decoded Fields

## Overview

This document details the analysis of the response payload returned by the Itron EverBlu Cyble Enhanced water meter via the RADIAN protocol.

The frame length byte advertises **124 bytes** (`0x7C`), and this is the true total length (data + 2-byte CRC trailer). With 4x-oversampled capture sized for 12-bit framing the firmware recovers the whole frame, and the CRC validates. See [Frame length and CRC](#frame-length-and-crc) below.

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
| [118-121]| 07 97 0B 00         | ✓ **DECODED** | **13th (most recent) history month** = 759,559 L (home_002) |
| [122-123]| 7A 60               | ✓ **DECODED** | **CRC-16/KERMIT** over bytes [0..121] (includes the length byte) |

## Successfully Decoded Fields

### Current reading and status

1. **Current Volume** [18-21] - Total litres since installation (uint32, LSB first)
2. **Meter real-time clock** [24-26, 28-30] - Day / month / year (20xx) / hour / minute / second
3. **Battery Remaining** [31] - Months until battery replacement
4. **Meter type / identifier** [32-42] - ASCII string (e.g. "133290AL02")
5. **Wake Hour** [44] / **Sleep Hour** [45] - Daily wake window (0-23)
6. **Read Counter** [48] - Number of times the meter has been interrogated

### Historical data

- **13 complete monthly volumes** [70-121] - Total volume at the end of each month, uint32 LSB first, oldest first (`70 + 13 × 4 = 122`, immediately followed by the CRC).

## Frame length and CRC

The frame is **124 bytes** total: `[0]` length (`0x7C`), `[1..121]` data (ending with the 13th history month at `[118-121]`), and the **CRC-16/KERMIT** at `[122-123]`.

- **Capture.** The on-air framing is 1 start + 8 data + 3 stop = 12 bits/byte at 4x oversampling, so a full 124-byte frame is ~748 raw bytes. Sizing the raw capture for 12-bit framing recovers the whole frame (a real decode also carries some trailing decoder noise past byte 123, which is ignored).
- **CRC convention (verified).** The CRC covers bytes **[0..121] including the length byte**, and the 2 CRC bytes sit at `[122-123]` (MSB first). Proven against two known-good on-device captures: `crc_kermit([0..121])` equals the `[122-123]` trailer (`0x7A60` and `0x88B8`). Computing over `[1..121]` (skipping the length byte) does **not** match; that off-by-one was a long-standing bug in `radian_validate_crc`, previously masked by the `length > size` compatibility shim on truncated captures.

> Historical note: earlier captures decoded only 120 bytes (truncating the 13th month + CRC) because the raw capture was sized for 11-bit framing. That, plus the CRC skipping byte 0, is why the CRC "never validated" before. Both are now fixed.

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

### ASCII Text (meter type / identifier)

Bytes [32-42] contain a null-terminated ASCII string that identifies the meter type:

```
31 33 33 32 39 30 41 4C 30 32 00
=  1  3  3  2  9  0  A  L  0  2 \0
= "133290AL02"
```

Observed across three meters of the same model (captured together):

| Meter serial | Type string |
| ------------ | ----------- |
| 257750       | `133290AL02` |
| 259301       | `047290AL02` |
| 259298       | `837290AL02` |

All three share the suffix `290AL02` and differ only in the leading three
digits. Because these were confirmed to be the same meter model, `290AL02` is
the shared model/family code and the leading `NNN` distinguishes the individual
meter (most likely a calibre / pulse-weight or factory variant index). The exact
sub-field breakdown is not documented in any datasheet, so the string is treated
as an opaque identifier and passed through verbatim. It is distinct from the
serial number (which comes from `meter_code` / frame bytes [9-13]) and is stable
across reads, which makes it a useful fingerprint in the test fixtures.

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
