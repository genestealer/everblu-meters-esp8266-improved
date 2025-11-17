# Adaptive Frequency Management Features

## Overview

Three new features have been implemented to make the firmware work reliably with any CC1101 module out-of-the-box, without requiring manual frequency tuning. These features address CC1101 module manufacturing tolerances that can cause frequency offsets.

## Motivation

Commercial utility meter readers work universally with all meters without per-device tuning. The issue preventing this firmware from working the same way is **CC1101 module tolerance**, not meter variance. Different CC1101 boards may have slightly different crystal frequencies or temperature characteristics, causing small frequency offsets (typically ±50-100 kHz).

## Features Implemented

### 1. Wide Initial Scan (First Boot Auto-Discovery)

**Function:** `performWideInitialScan()`

**When it runs:** Automatically triggered during `setup()` if no valid frequency offset is found in EEPROM/Preferences.

**What it does:**
- Performs a **coarse scan** of ±100 kHz around the configured base frequency (433.82 MHz default)
  - Step size: 10 kHz
  - ~21 frequency points tested
- When a signal is found, performs a **fine scan** of ±15 kHz around the best coarse result
  - Step size: 3 kHz  
  - ~11 frequency points tested
- Saves the discovered frequency offset to EEPROM for future boots
- Publishes results to MQTT (`everblu/cyble/frequency_offset`)

**Benefits:**
- New users can flash the firmware and it will automatically find their meter's frequency
- No manual frequency scanning or tuning required
- Makes the project more accessible to the community

**Serial output example:**
```
> No stored frequency offset found. Performing wide initial scan...
> Performing wide initial scan (first boot - no saved offset)...
> Scanning from 433.720000 to 433.920000 MHz (step: 0.010000 MHz)
> Found signal at 433.767000 MHz: RSSI=-85 dBm
> Performing fine scan around 433.767000 MHz...
> Refined signal at 433.767029 MHz: RSSI=-84 dBm
> Initial scan complete! Best frequency: 433.767029 MHz (offset: -0.052971 MHz, RSSI: -84 dBm)
```

### 2. Adaptive Frequency Tracking

**Function:** `adaptiveFrequencyTracking(int8_t freqest)`

**When it runs:** After every successful meter read in `onUpdateData()`.

**What it does:**
- Reads the CC1101's FREQEST register, which measures the actual frequency offset detected by the radio's FOC (Frequency Offset Compensation) circuitry
- Accumulates frequency error over 10 successful reads
- If the average error exceeds 2 kHz (significant drift), adjusts the stored frequency offset
- Applies a conservative 50% correction factor to avoid over-adjustment
- Saves the adjusted offset to EEPROM and reinitializes the CC1101

**Benefits:**
- Compensates for CC1101 module drift due to temperature changes
- Compensates for CC1101 crystal aging over time
- Keeps the radio locked on the meter's frequency for optimal reception
- No user intervention needed

**Technical details:**
- FREQEST resolution: ~1.59 kHz per LSB (based on 26 MHz crystal: Fxosc/2^14)
- Threshold: 2 kHz average error over 10 reads
- Correction: 50% of measured error applied per adjustment cycle

**Serial output example:**
```
> Adaptive tracking: FREQEST=-3, freq_error_mhz=-0.004762 MHz
> Cumulative error: -0.028571 MHz over 6 reads
[After 10 reads with consistent error...]
> Adaptive tracking triggered: avg error -0.003000 MHz over 10 reads
> Applying 50% correction: -0.001500 MHz
> New total offset: -0.054471 MHz
> Reinitializing CC1101 with adjusted frequency...
```

### 3. Enhanced FOC Configuration

**Status:** Already optimal in existing code

**Current setting:** `FOCCFG = 0x1D`
- FOC_BS_CS_GATE = 0 (FOC algorithm runs before CS qualifier)
- FOC_PRE_K = 3K (medium loop gain before sync word)
- FOC_POST_K = K/2 (reduced loop gain after sync word)
- FOC_LIMIT = 0 (±Fxosc/4 offset range)

This configuration is already well-tuned for the Itron EverBlu meter characteristics and provides good real-time frequency offset compensation.

## Integration Points

### In setup() function:
```cpp
// Load stored frequency offset
storedFrequencyOffset = loadFrequencyOffset();

// If no valid frequency offset found and auto-scan is enabled, perform wide initial scan
if (storedFrequencyOffset == 0.0 && autoScanEnabled) {
  Serial.println("> No stored frequency offset found. Performing wide initial scan...");
  performWideInitialScan();
  // Reload the frequency offset after scan
  storedFrequencyOffset = loadFrequencyOffset();
}
```

### In onUpdateData() function:
```cpp
// After successful meter read and all MQTT publishes...

// Perform adaptive frequency tracking based on FREQEST register
adaptiveFrequencyTracking(meter_data.freqest);
```

## Data Flow

1. **First Boot:**
   ```
   setup() → loadFrequencyOffset() returns 0.0
           → performWideInitialScan() finds frequency
           → saveFrequencyOffset(offset)
           → cc1101_init(base + offset)
   ```

2. **Subsequent Boots:**
   ```
   setup() → loadFrequencyOffset() returns saved offset
           → Skip wide scan (offset already known)
           → cc1101_init(base + offset)
   ```

3. **During Operation:**
   ```
   onUpdateData() → get_meter_data() [includes FREQEST]
                  → Publish all data to MQTT
                  → adaptiveFrequencyTracking(freqest)
                  → [Every 10 reads: adjust offset if needed]
   ```

## Configuration Options

### Disable Auto-Scan
Set in `private.h`:
```cpp
#define AUTO_SCAN_ENABLED false  // Disable automatic wide initial scan
```

### Manual Frequency Scan
Users can still trigger a manual frequency scan via MQTT:
```bash
mosquitto_pub -t "everblu/cyble/frequency_scan" -m "START"
```

## MQTT Topics

### Published by these features:
- `everblu/cyble/frequency_offset` - Current frequency offset in MHz (retained)
- `everblu/cyble/cc1101_state` - "Initial Frequency Scan", "Adjusting Frequency", "Idle"
- `everblu/cyble/status_message` - Detailed status messages about scans and adjustments

### Monitored:
- `everblu/cyble/frequency_scan` - User can request manual scan

## When to Clear EEPROM

**Always clear EEPROM when you change hardware:**

1. **Replace ESP8266/ESP32 board** - Different boards may have slightly different CC1101 connections or noise characteristics
2. **Replace CC1101 radio module** - Each CC1101 has unique crystal tolerance (±10-50 kHz typical)
3. **Move to a different meter** - Different meters may transmit on slightly different frequencies

**How to clear EEPROM:**

In `include/private.h`, set:
```cpp
#define CLEAR_EEPROM_ON_BOOT 1
```

Upload firmware, wait for one boot cycle (wide scan will run), then set back to:
```cpp
#define CLEAR_EEPROM_ON_BOOT 0
```

Upload again to preserve the discovered frequency.

**Why this matters:** The stored frequency offset is specific to your CC1101 module. Using a different CC1101 with the old offset may prevent successful meter communication.

## Testing Recommendations

### Test 1: First Boot Behavior
1. Set `CLEAR_EEPROM_ON_BOOT = 1` in `private.h`
2. Upload firmware and power on ESP8266
3. Watch serial monitor for wide scan progress (~1-2 minutes)
4. Verify frequency offset is saved to EEPROM
5. Set `CLEAR_EEPROM_ON_BOOT = 0` and upload again
6. Verify subsequent boots skip the wide scan and use stored offset

### Test 2: Adaptive Tracking
1. Monitor serial output over 10+ successful meter reads
2. Note FREQEST values and cumulative error
3. If drift occurs (e.g., temperature change), verify adjustment happens
4. Check MQTT `frequency_offset` topic updates

### Test 3: Temperature Drift
1. Place ESP8266 in cold/hot environment
2. Wait for temperature stabilization (~30 min)
3. Monitor adaptive tracking adjustments
4. Verify meter reads remain successful

## Troubleshooting

### Wide scan finds no signal:
- Check meter is within range (typically < 50m)
- Verify meter is in wake window (check time_start/time_end in previous reads)
- Check CC1101 wiring and antenna connection
- Try increasing scan range in code (currently ±100 kHz)

### Adaptive tracking keeps adjusting:
- Normal if temperature is fluctuating
- If oscillating, consider reducing correction factor from 50% to 25%
- Check CC1101 module quality - poor modules may have unstable crystals

### FREQEST always reads 0:
- Verify FREQEST field was added to tmeter_data struct in cc1101.h
- Verify get_meter_data() reads FREQEST register in cc1101.cpp
- Check FOC is enabled (FOCCFG should be 0x1D, not 0x00)

## Code References

### Key files modified:
- `src/main.cpp` - Main functions and integration
- `src/cc1101.h` - Added freqest field to tmeter_data
- `src/cc1101.cpp` - Read FREQEST register in get_meter_data()

### New functions added:
- `void performWideInitialScan()` - Line ~1362
- `void adaptiveFrequencyTracking(int8_t freqest)` - Line ~1457

### Integration points:
- `setup()` - Line ~1514 (wide scan call)
- `onUpdateData()` - Line ~343 (adaptive tracking call)

## Future Enhancements

Possible improvements if needed:
1. **Adaptive scan range:** If wide scan fails, automatically increase range
2. **Temperature sensor:** Use ESP8266 internal temp to predict drift
3. **Smart threshold:** Adjust adaptation threshold based on signal quality
4. **Multi-meter support:** Track different offsets for multiple meters
5. **Calibration history:** Log frequency adjustments to identify failing hardware

## Credits

These features were inspired by the observation that commercial utility meter reading equipment works universally without per-device tuning, demonstrating that automatic frequency adaptation is both practical and necessary for a robust open-source meter reader.
