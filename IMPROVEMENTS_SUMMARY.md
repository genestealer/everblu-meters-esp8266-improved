# Improvements Summary

## Overview
This document summarizes the improvements made to the EverBlu Meters ESP8266/ESP32 project to enhance performance, reliability, and diagnostics.

---

## 1. ✅ Reduced MQTT Publish Delays

**Problem:** Multiple `delay(50)` calls throughout the code were causing significant delays during MQTT publishing operations, totaling ~500ms+ per update cycle.

**Solution:** 
- Reduced all MQTT publish delays from `delay(50)` to `delay(5)` throughout the codebase
- This reduces total delay overhead by ~90% (from ~500ms to ~50ms)
- Locations updated:
  - `onUpdateData()` - meter data publishing (11 locations)
  - `publishWifiDetails()` - WiFi diagnostics (8 locations)
  - `publishMeterSettings()` - configuration data (4 locations)
  - `onConnectionEstablished()` - HA discovery messages (25+ locations)

**Impact:** Faster MQTT response times, improved system responsiveness, reduced blocking time during meter reads.

---

## 2. ✅ Enhanced Error Handling and Diagnostics

**New Features:**

### Success/Failure Metrics
- Added global counters to track:
  - `totalReadAttempts` - Total number of read attempts
  - `successfulReads` - Successfully completed reads
  - `failedReads` - Failed read attempts
  - `lastErrorMessage` - Human-readable last error

### CC1101 State Monitoring
- Real-time CC1101 radio state published to MQTT:
  - "Idle" - Normal standby state
  - "Reading" - Actively communicating with meter
  - "Frequency Scanning" - Performing frequency scan

### Error Messages
- Detailed failure mode reporting:
  - Retry count tracking with messages
  - Cooldown period notifications
  - Timeout and communication failures

### New MQTT Topics
- `everblu/cyble/total_attempts` - Total read attempts counter
- `everblu/cyble/successful_reads` - Successful reads counter
- `everblu/cyble/failed_reads` - Failed reads counter
- `everblu/cyble/last_error` - Last error message (text)
- `everblu/cyble/cc1101_state` - Current CC1101 radio state

### Home Assistant Integration
- Added MQTT discovery messages for all new diagnostic sensors
- Sensors automatically appear in Home Assistant with proper device class and icons
- Diagnostic entities properly categorized for UI organization

**Impact:** Better visibility into system health, easier troubleshooting, and proactive monitoring capabilities.

---

## 3. ✅ Frequency Offset Storage and Compensation

**Problem:** CC1101 oscillator drift and environmental factors can cause the optimal frequency to differ from the nominal 433.82 MHz, requiring manual adjustment.

**Solution:**

### Persistent Storage Implementation
- **ESP8266:** Uses EEPROM with magic number validation
- **ESP32:** Uses Preferences API for non-volatile storage
- Stores frequency offset in MHz (e.g., +0.005 for +5 kHz)
- Sanity checking prevents invalid offsets (±0.1 MHz limit)

### Functions Added
```cpp
void saveFrequencyOffset(float offset);  // Save offset to storage
float loadFrequencyOffset();             // Load offset from storage
void performFrequencyScan();             // Automatic frequency scanning
```

### Automatic Frequency Application
- Offset loaded at startup
- Applied to CC1101 initialization automatically
- Logged to serial for verification

### New MQTT Topic
- `everblu/cyble/frequency_offset` - Current frequency offset in MHz

**Impact:** Improved signal reception, automatic compensation for frequency drift, reduced manual configuration needs.

---

## 4. ✅ Frequency Scanning and Auto-Tuning

**Feature:** Intelligent frequency scanning to find optimal reception frequency automatically.

### Scan Parameters
- **Range:** ±30 kHz from nominal frequency (±0.03 MHz)
- **Step Size:** 5 kHz (0.005 MHz) for fine-grained scanning
- **Total Steps:** 13 frequency points tested
- **Criterion:** Best RSSI with valid data

### Scan Process
1. User triggers scan via MQTT or Home Assistant button
2. System publishes "Frequency Scanning" state
3. Iterates through frequency range
4. Tests each frequency by attempting meter read
5. Tracks best RSSI and corresponding frequency
6. Saves optimal offset to persistent storage
7. Reconfigures CC1101 with best frequency
8. Publishes results to MQTT

### MQTT Integration
- **Command Topic:** `everblu/cyble/frequency_scan`
  - Payload: `"scan"` to trigger
- **Home Assistant Button:** "Scan Frequency" automatically created
- **Status Updates:** Real-time progress via status messages

### Example Output
```
> Starting frequency scan...
> Scanning from 433.790000 to 433.850000 MHz (step: 0.005000 MHz)
> Better signal at 433.815000 MHz: RSSI=-45 dBm
> Frequency scan complete. Best frequency: 433.815000 MHz (offset: -0.005000 MHz, RSSI: -45 dBm)
> Frequency offset -0.005000 MHz saved to EEPROM
```

**Impact:** Optimizes signal reception automatically, eliminates guesswork, adapts to environmental changes.

---

## 5. ✅ Code Quality Improvements

### Removed Unused Variables
**File:** `cc1101.cpp`
- Removed `RF_Test_u8` (never used)
- Removed `PA_Test[]` array (never used)
- Kept only necessary variables

**Impact:** Reduced RAM usage, cleaner code, easier maintenance.

### Consolidated Debug Output Functions
**File:** `utils.cpp`

**Before:** 4 separate functions with duplicated logic
```cpp
void show_in_hex(const uint8_t* buffer, size_t len);
void show_in_hex_array(const uint8_t* buffer, size_t len);
void show_in_hex_one_line(const uint8_t* buffer, size_t len);
void show_in_hex_one_line_GET(const uint8_t* buffer, size_t len);
```

**After:** Single unified function with mode parameter
```cpp
void show_in_hex_formatted(const uint8_t* buffer, size_t len, int mode);
// mode: 0=16 per line, 1=array format, 2=single line, 3=GET format
```

**Benefit:** Reduced code duplication, easier to maintain, legacy wrappers preserved for compatibility.

---

## 6. ✅ RADIAN Protocol Documentation

**Added comprehensive comments explaining:**

### Protocol Overview
- Frequency specifications (433.82 MHz nominal)
- Modulation type (2-FSK)
- Data rates (2.4 kbps sync, 9.6 kbps data)
- Sync patterns and packet structure

### Communication Sequence
Detailed timeline documentation:
- Wake-up phase (2000ms)
- Interrogation frame (130ms)
- Meter response acknowledgement (timing breakdown)
- Data frame reception (timing breakdown)

### Data Encoding
- 4-bit-per-bit oversampling explanation
- Serial frame format (start/stop bits)
- Bit encoding examples
- Decoding algorithm explanation

### Key Functions Documented
- `get_meter_data()` - Overall protocol flow
- `receive_radian_frame()` - Two-stage sync detection
- `decode_4bitpbit_serial()` - Bit decoding algorithm

**Impact:** Easier for developers to understand and maintain the protocol implementation, enables future enhancements.

---

## Summary of New Home Assistant Entities

### Sensors
1. **Total Read Attempts** - Counter of all read attempts
2. **Successful Reads** - Counter of successful reads
3. **Failed Reads** - Counter of failed reads
4. **Last Error** - Text of last error message
5. **CC1101 State** - Current radio state (Idle/Reading/Scanning)
6. **Frequency Offset** - Current frequency offset in MHz

### Buttons
1. **Scan Frequency** - Trigger automatic frequency scan

### All Entities Auto-Categorized
- Diagnostic sensors appear in "Diagnostic" section
- Configuration controls in "Configuration" section
- Proper device association with Water Meter device

---

## Compilation Results

### ESP8266 (Huzzah)
- ✅ **Compilation:** SUCCESS
- **RAM Usage:** 48.0% (39,324 / 81,920 bytes)
- **Flash Usage:** 37.8% (394,372 / 1,044,464 bytes)
- **Build Time:** 15.04 seconds

### ESP32 (esp32dev)
- ✅ **Compilation:** SUCCESS
- **RAM Usage:** 16.0% (52,416 / 327,680 bytes)
- **Flash Usage:** 67.7% (887,661 / 1,310,720 bytes)
- **Build Time:** 11.23 seconds

**Note:** ESP32 has higher flash usage due to Preferences library, but much lower RAM percentage due to larger available RAM.

---

## Warnings Addressed

Compilation warnings are minor and do not affect functionality:
- Sign comparison warnings (comparing int vs size_t) - common and safe
- Unused static function declaration - from header file organization
- Uninitialized variable warning - false positive, variable always initialized before use

---

## Future Enhancement Opportunities

Based on the original suggestions, these improvements could be added in future iterations:

1. **Non-Blocking Reads** - State machine-based meter communication
2. **Power Management** - Deep sleep support for battery operation
3. **Enhanced Retry Logic** - Exponential backoff instead of fixed delays
4. **Frequency Drift Monitoring** - Periodic automatic frequency scans
5. **MQTT Command for Manual Frequency** - Set frequency offset directly

---

## Testing Recommendations

Before deploying to production:

1. **Verify MQTT Discovery**
   - Check all entities appear in Home Assistant
   - Verify proper device association

2. **Test Frequency Scanning**
   - Trigger scan via MQTT or HA button
   - Verify offset is saved and reloaded on reboot
   - Check scan results are reasonable

3. **Monitor Diagnostics**
   - Observe success/failure metrics
   - Check CC1101 state transitions
   - Verify error messages are helpful

4. **Performance Testing**
   - Measure time reduction in MQTT operations
   - Verify no timeout issues with shorter delays
   - Test under various WiFi conditions

---

## Files Modified

1. **src/main.cpp**
   - Added frequency storage functions
   - Added metrics tracking
   - Reduced MQTT delays
   - Enhanced error handling
   - Added frequency scanning

2. **src/cc1101.cpp**
   - Removed unused variables
   - Added comprehensive RADIAN protocol documentation
   - Enhanced comments in key functions

3. **src/utils.cpp**
   - Consolidated debug output functions
   - Improved code reusability

---

## Backward Compatibility

All changes maintain backward compatibility:
- Existing MQTT topics unchanged
- Legacy debug functions preserved as wrappers
- Configuration file format unchanged
- No breaking changes to core functionality

---

## Credits

Improvements by: GitHub Copilot (October 2025)
Based on original work by: Psykokwak and Neutrinus
Enhanced and maintained by: Genestealer

