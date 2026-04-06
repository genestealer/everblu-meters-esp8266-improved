# Troubleshooting Corrupted or Invalid Volume Readings

This guide helps diagnose and fix issues with corrupted, invalid, or unexpected volume readings from your Itron EverBlu Cyble Enhanced meter.

## Symptoms

- Volume reads as 0, 13, or other unrealistically low values
- Volume shows 0xFFFFFFFF (-1) or appears as negative numbers
- Historical data shows impossible decreases
- Battery shows as 0 months
- Wake/sleep times show invalid values (e.g., 85-0)
- Timestamp shows 1970-01-02 (Unix epoch)

**These symptoms indicate:**
✅ RF communication is working (meter responds to interrogation)
✅ Manchester encoding/decoding is working
✅ Frame structure is correct
❌ **Data parsing is failing** - likely wrong byte offsets for your meter variant

---

## Step 1: Enable Raw Byte Hex Dump

The firmware can output the raw 200 decoded bytes in hex format for analysis.

### ESPHome Version

Add to your YAML configuration under the `everblu_meter:` section:

```yaml
everblu_meter:
  id: my_water_meter
  meter_year: 15
  meter_serial: 5020386
  # ... other settings ...
  
  debug_cc1101: true  # ⬅️ Add this line
```

Recompile and flash:
```bash
esphome run your-config.yaml
```

### MQTT/PlatformIO Version

Edit `include/private.h`:

```cpp
#define DEBUG_CC1101 1  // Change from 0 to 1
```

Rebuild and upload:
```bash
pio run --target upload
```

---

## Step 2: Trigger a Read and Capture Logs

### ESPHome Logs

View logs via:
- **ESPHome web interface**: `http://water-meter.local:6052`
- **Home Assistant**: Developer Tools → Logs
- **Command line**: `esphome logs your-config.yaml`

### MQTT Logs

View via:
- **Serial monitor**: USB connection to ESP device
- **WiFi Serial Monitor**: Configure in `private.h` (see [WIFI_SERIAL_MONITOR.md](WIFI_SERIAL_MONITOR.md))

---

## Step 3: Analyze the Hex Dump Output

### Normal Mode Output (debug_cc1101: false)

Shows first 32 bytes (header + volume field):

```
[METER] First 32 bytes (header + volume field): 7C 11 00 45 20 0A 50 14 ...
```

### Full Debug Mode Output (debug_cc1101: true)

Shows complete 200-byte frame with offset markers:

```
[CC1101] Full hex dump of decoded frame (200 bytes):
Offset  : Hex Data
[000-015]: 7C 11 00 45 20 0A 50 14 12 03 00 20 40 14 03 00 
[016-031]: 6E 17 EE 01 0B 00 40 06 1E 0A 50 14 12 01 00 65 
[032-047]: 31 33 33 32 39 20 30 41 4C 30 32 00 00 06 12 04 
[048-063]: 01 30 75 04 C0 6D 05 00 02 81 9A 20 12 09 00 00 
[064-079]: 00 00 23 9A 08 00 97 CA 08 00 36 EE 08 00 B6 1E 
...
Note: Bytes [18-21]=volume, [31]=battery, [44-45]=wake/sleep, [66-117]=history
```

### Error-Triggered Hex Dumps

When validation fails, additional debug output shows the problematic bytes:

**Invalid Volume (0 or 0xFFFFFFFF):**
```
[ERROR] Parsed volume value is invalid (0x00000000) - discarding frame
[DEBUG] Volume bytes [18-21]: 00 00 00 00
[DEBUG] First 32 bytes of frame: 7C 11 00 45 20 0A 50 14 ...
```

**CRC Mismatch:**
```
[ERROR] RADIAN CRC mismatch (computed=0x3A5C frame=0x0000) - discarding frame
[DEBUG] Frame bytes [0-31]: 7C 11 00 45 20 0A 50 14 ...
```

**Historical Data Corruption:**
```
[ERROR] Historical volume decreased at index 2 (81920 -> 0) - marking history invalid
[DEBUG] Historical bytes [74-77]: 00 00 00 00 (index 2)
```

---

## Step 4: Decode the Hex Data

### Known Byte Positions (Standard Itron EverBlu Cyble Enhanced)

| Bytes | Field | Format | Example | Decoded Value |
|-------|-------|--------|---------|---------------|
| [0] | Frame Length | uint8 | `7C` | 124 bytes |
| [18-21] | **Current Volume** | uint32 LSB-first | `EE 01 0B 00` | 0x000B01EE = 721,390 L |
| [31] | **Battery (months)** | uint8 | `65` | 101 months |
| [44] | **Wake Hour** | uint8 | `06` | 06:00 (6am) |
| [45] | **Sleep Hour** | uint8 | `12` | 18:00 (6pm) |
| [48] | **Read Counter** | uint8 | `30` | 48 reads |
| [66-69] | History Month -12 | uint32 LSB-first | `23 9A 08 00` | 564,771 L |
| [70-73] | History Month -11 | uint32 LSB-first | `97 CA 08 00` | 576,151 L |
| ... | ... | ... | ... | ... |
| [114-117] | History Month -1 | uint32 LSB-first | `10 E0 0A 00` | 713,744 L |

### Decode uint32 LSB-First Values

For a 4-byte little-endian (LSB-first) value like `EE 01 0B 00`:

```
Volume = (0x00 << 24) | (0x0B << 16) | (0x01 << 8) | 0xEE
       = 0x000B01EE
       = 721,390 liters
```

---

## Step 5: Compare Your Data

### Check Volume Field (Bytes 18-21)

From your hex dump, extract bytes [18-21]:

**Example from Issue #52:**
```
[DEBUG] Volume bytes [18-21]: 00 00 00 00
```
This decodes to volume = 0, which is clearly wrong for a meter installed in 2015.

**Expected for 10-year-old meter:**
Something like `D0 84 07 00` → ~500,000 liters (typical household cumulative usage)

### Hypothesis: Wrong Byte Offset

If your volume field contains zeros but the meter is working, the actual volume data might be at a different byte position. Look for a plausible uint32 value elsewhere in the first 32 bytes.

**Search Pattern:**
- Look for 4 consecutive bytes that decode to a reasonable consumption value
- Typical domestic water meter after 10 years: 200,000 to 1,000,000 liters
- In hex: roughly `0x00030000` to `0x000F0000`

---

## Step 6: Regional/Variant Differences

### Known Variants

Different meter variants or manufacturing dates may use different data layouts:

| Variant | Region | Year Range | Volume Offset | Notes |
|---------|--------|------------|---------------|-------|
| Standard | France/EU | 2018+ | Bytes [18-21] | Most common, tested format |
| UK/Older | UK | 2010-2017 | **Unknown** | Possible different offset ❗ |
| Encrypted | Various | Recent | N/A | AES-128 encrypted payload |

**Issue #52 Meter:**
- Model: Itron EverBlu Cyble Enhanced
- Region: UK
- Install Year: 2015
- **Suspected Issue**: Different byte layout than standard 2018+ European meters

---

## Step 7: Manual Byte Analysis

If your volume field is at a different offset, try this systematic search:

### Python Script to Find Volume Field

```python
# Parse your hex dump into a byte array
hex_string = "7C 11 00 45 20 0A 50 14 12 03 00 20 40 14 03 00 6E 17 00 00 00 00 ..."
bytes_data = [int(b, 16) for b in hex_string.split()]

# Expected volume (from meter display or utility bill)
expected_volume = 500000  # Adjust to your actual meter reading

# Search for this value as uint32 LSB-first
target_bytes = [
    expected_volume & 0xFF,
    (expected_volume >> 8) & 0xFF,
    (expected_volume >> 16) & 0xFF,
    (expected_volume >> 24) & 0xFF
]

print(f"Looking for bytes: {' '.join(f'{b:02X}' for b in target_bytes)}")

for i in range(len(bytes_data) - 3):
    if bytes_data[i:i+4] == target_bytes:
        print(f"✅ Found volume at offset [{i}-{i+3}]")
        
    # Also try as big-endian (MSB-first)
    target_reversed = list(reversed(target_bytes))
    if bytes_data[i:i+4] == target_reversed:
        print(f"✅ Found volume (big-endian) at offset [{i}-{i+3}]")
```

---

## Step 8: Report Findings

If you discover a different byte layout, please report it to help others:

### What to Include

1. **Meter Details:**
   - Model/Serial number
   - Manufacturing year
   - Region/country
   - Any visible model text

2. **Hex Dump:**
   - Full 200-byte hex dump from a working read
   - Multiple samples if possible

3. **Known Values:**
   - Actual volume from meter display
   - Battery status if visible
   - Any other readable data

4. **Where to Report:**
   - GitHub Issue: https://github.com/genestealer/everblu-meters-esp8266-improved/issues
   - Include [METER-VARIANT] in title
   - Reference this troubleshooting guide

---

## Step 9: Code Modifications (Advanced)

If you've identified the correct byte offsets, you can modify the parsing code:

### Edit parse_meter_report() Function

File: `ESPHOME-release/everblu_meter/cc1101.cpp` (or `src/core/cc1101.cpp` for MQTT)

**Current volume extraction (line ~815):**
```cpp
data.volume = ((uint32_t)decoded_buffer[18]) |
              ((uint32_t)decoded_buffer[19] << 8) |
              ((uint32_t)decoded_buffer[20] << 16) |
              ((uint32_t)decoded_buffer[21] << 24);
```

**Change byte offsets as needed:**
```cpp
// Example: if volume is actually at bytes [22-25]
data.volume = ((uint32_t)decoded_buffer[22]) |
              ((uint32_t)decoded_buffer[23] << 8) |
              ((uint32_t)decoded_buffer[24] << 16) |
              ((uint32_t)decoded_buffer[25] << 24);
```

**Similarly for other fields:**
- Battery: `decoded_buffer[31]` → adjust offset
- Counter: `decoded_buffer[48]` → adjust offset
- Times: `decoded_buffer[44]`, `decoded_buffer[45]` → adjust offsets

---

## Common Patterns

### Pattern 1: All Zeros
```
Volume bytes [18-21]: 00 00 00 00
```
**Cause:** Wrong byte offset, or meter hasn't transmitted real data
**Solution:** Check other byte positions for expected volume

### Pattern 2: All Ones (0xFFFFFFFF)
```
Volume bytes [18-21]: FF FF FF FF
```
**Cause:** Uninitialized memory area, definitely wrong offset
**Solution:** Volume field is elsewhere in frame

### Pattern 3: Single Bit Values
```
Volume reads as: 1, 2, 4, 8, 16, 32...
```
**Cause:** Reading single bits instead of full 32-bit value
**Solution:** Check bit-shifting logic in volume extraction

### Pattern 4: Garbage Data But Valid CRC
```
CRC valid ✅
Volume: 13 L (clearly wrong)
Battery: 0 months (impossible)
```
**Cause:** CRC validates frame integrity, not semantic correctness. Different meter variant with same protocol but different data layout.

---

## Quick Reference

### Enable Hex Dump
- **ESPHome**: `debug_cc1101: true` in YAML
- **MQTT**: `#define DEBUG_CC1101 1` in private.h

### Key Byte Positions (Standard)
- **Volume**: [18-21] uint32 LSB-first
- **Battery**: [31] uint8 months
- **Counter**: [48] uint8 
- **Wake/Sleep**: [44-45] uint8 hours

### Analysis Tools
- Python script above for systematic search
- Online hex calculator: https://www.rapidtables.com/convert/number/hex-to-decimal.html
- ESPHome logs viewer: `esphome logs config.yaml`

---

## Additional Resources

- [PAYLOAD_ANALYSIS.md](PAYLOAD_ANALYSIS.md) - Complete byte-by-byte breakdown
- [API_DOCUMENTATION.md](API_DOCUMENTATION.md) - Parsing function documentation
- [ESPHOME_INTEGRATION_GUIDE.md](../ESPHOME/docs/ESPHOME_INTEGRATION_GUIDE.md) - ESPHome setup
- [GitHub Issues](https://github.com/genestealer/everblu-meters-esp8266-improved/issues) - Community support

---

## Success Checklist

- [ ] Enabled `debug_cc1101` in configuration
- [ ] Triggered manual meter read
- [ ] Captured hex dump from logs
- [ ] Identified volume bytes [18-21]
- [ ] Calculated expected volume value
- [ ] Searched hex dump for expected bytes
- [ ] Found volume at different offset (if applicable)
- [ ] Modified code to use correct offset
- [ ] Tested and verified correct readings
- [ ] Reported findings to project
