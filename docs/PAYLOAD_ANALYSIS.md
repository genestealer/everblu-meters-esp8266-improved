# Meter Payload Analysis - Decoded Fields

## Overview
This document details the complete analysis of the 122-byte payload returned by the Itron EverBlu Cyble Enhanced water meter via the RADIAN protocol.

## Complete Payload Map

### Byte-by-Byte Breakdown

| Bytes    | Hex Value (Example) | Status | Description |
|----------|---------------------|--------|-------------|
| [0]      | 7C                  | ✓ Known | Frame length indicator (0x7C = 124 decimal) |
| [1-2]    | 11 00               | ? Unknown | Protocol header/flags |
| [3-17]   | 45 20 0A 50 14...   | ? Unknown | Protocol header/control data |
| [18-21]  | EE 01 0B 00         | ✓ **DECODED** | **Current volume (liters)** - uint32_t LSB first<br>Example: 0x000B01EE = 721,390 liters |
| [22-30]  | 40 06 1E 0A...      | ? Unknown | Unknown fields |
| [31]     | 65                  | ✓ **DECODED** | **Battery remaining (months)**<br>Example: 0x65 = 101 months |
| [32-42]  | 31 33 33 32 39...   | ✓ Known | **Meter model/serial (ASCII text)**<br>Example: "13329 0AL02\0" |
| [43]     | 00                  | ? Unknown | Unknown |
| [44]     | 06                  | ✓ **DECODED** | **Wake hour (24h format)**<br>Example: 0x06 = 06:00 |
| [45]     | 12                  | ✓ **DECODED** | **Sleep hour (24h format)**<br>Example: 0x12 = 18:00 (6pm) |
| [46-47]  | 04 01               | ? Unknown | Unknown |
| [48]     | C7                  | ✓ **DECODED** | **Read counter**<br>Example: 0xC7 = 199 times meter has been read |
| [49-53]  | 01 00 00 00 00      | ? Unknown | Unknown fields |
| [54-65]  | 00 00 80 80 80...   | ✓ Known | **Reserved/unused fields** (0x80 = "no data" marker) |
| [66-69]  | B6 E7 08 00         | ✓ **DECODED** | **Historical volume - Month -13** (oldest)<br>Example: 583,606 liters |
| [70-73]  | E1 01 09 00         | ✓ **DECODED** | **Historical volume - Month -12**<br>Example: 590,305 liters |
| [74-77]  | B6 1E 09 00         | ✓ **DECODED** | **Historical volume - Month -11**<br>Example: 597,686 liters |
| [78-81]  | 00 3E 09 00         | ✓ **DECODED** | **Historical volume - Month -10**<br>Example: 605,696 liters |
| [82-85]  | DB 5E 09 00         | ✓ **DECODED** | **Historical volume - Month -9**<br>Example: 614,107 liters |
| [86-89]  | 59 7B 09 00         | ✓ **DECODED** | **Historical volume - Month -8**<br>Example: 621,401 liters |
| [90-93]  | CB 9D 09 00         | ✓ **DECODED** | **Historical volume - Month -7**<br>Example: 629,195 liters |
| [94-97]  | 36 C4 09 00         | ✓ **DECODED** | **Historical volume - Month -6**<br>Example: 639,030 liters |
| [98-101] | F5 F5 09 00         | ✓ **DECODED** | **Historical volume - Month -5**<br>Example: 652,789 liters |
| [102-105]| 31 2F 0A 00         | ✓ **DECODED** | **Historical volume - Month -4**<br>Example: 667,441 liters |
| [106-109]| B6 70 0A 00         | ✓ **DECODED** | **Historical volume - Month -3**<br>Example: 684,214 liters |
| [110-113]| F5 B1 0A 00         | ✓ **DECODED** | **Historical volume - Month -2**<br>Example: 700,917 liters |
| [114-117]| 10 E0 0A 00         | ✓ **DECODED** | **Historical volume - Month -1** (most recent complete month)<br>Example: 713,744 liters |
| [118-121]| ?? ?? ?? ??         | ? Unknown | Unknown (payload typically 122 bytes, expected 124) |

## Successfully Decoded Fields

### Currently Exposed to Home Assistant (Before This Update)
1. **Current Volume** [bytes 18-21] - Total liters accumulated since meter installation
2. **Battery Remaining** [byte 31] - Months until battery replacement needed
3. **Read Counter** [byte 48] - Number of times meter has been interrogated
4. **Wake Hour** [byte 44] - Start of daily wake window (0-23)
5. **Sleep Hour** [byte 45] - End of daily wake window (0-23)

### Newly Decoded (This Update)
6. **13 Months Historical Volumes** [bytes 66-117] - Total volume at end of each of the last 13 months

### Identified But Not Decoded
7. **Meter Model/Serial ASCII** [bytes 32-42] - Device identifier text (e.g., "13329 0AL02")
   - Could be useful as a device identifier
   - Currently not extracted programmatically

## Unknown/Undecoded Fields

### Header/Control Bytes [1-2], [3-17], [22-30], [43], [46-47], [49-53]
These bytes likely contain:
- **CRC checksums** - Protocol integrity verification
- **Frame sequence numbers** - For tracking multiple queries/responses
- **Protocol version indicators**
- **Meter status flags** - Error conditions, alarms, etc.
- **Encryption/authentication data** - Though RADIAN is not known to use encryption

Without official Itron protocol documentation, reverse engineering these fields would require:
1. Capturing many different meter responses under varying conditions
2. Comparing payloads from multiple meter models
3. Analyzing bit patterns for flags/counters
4. Testing meter in error conditions to see status flag changes

### Missing Bytes [118-121]
The frame length byte indicates 124 bytes (0x7C), but we only receive 122 bytes. This discrepancy could be:
- **CRC bytes** - Checksum at the end (not included in decoded buffer)
- **Framing overhead** - Start/stop bits that are stripped during 4-bit-per-bit decoding
- **Padding** - Reserved space for future use

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
- **Meter Model**: Itron EverBlu Cyble Enhanced (e.g., 2020-0257750)
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
