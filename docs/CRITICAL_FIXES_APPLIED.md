# Critical Security and Stability Fixes Applied

**Date:** October 29, 2025  
**Branch:** automatic-calibration

## Summary

Four critical issues identified in the code review have been successfully fixed to improve system stability, prevent crashes, and enhance security.

---

## ✅ Fix #1: Watchdog Timer Protection

### Problem
Long-running loops could trigger watchdog timer resets, causing unexpected device reboots, especially during:
- Frequency scanning (up to 2 minutes)
- WUP transmission (up to 3 seconds)
- Frame reception (up to 1 second)

### Solution
Added `FEED_WDT()` calls in all critical loops to prevent watchdog resets.

### Changes Made

#### `src/main.cpp`
- **Lines 17-27:** Added cross-platform `FEED_WDT()` macro
  - ESP8266: Uses `ESP.wdtFeed()`
  - ESP32: Uses `esp_task_wdt_reset()` + `yield()`
- **Line 1377:** Added in `performFrequencyScan()` loop (±30 kHz scan)
- **Line 1400:** Added in `performWideInitialScan()` coarse scan loop (±100 kHz)
- **Line 1419:** Added in `performWideInitialScan()` fine scan loop (±15 kHz)

#### `src/cc1101.cpp`
- **Line 948:** Simplified watchdog feeding in WUP transmission loop (consolidated 3 calls to 1)
- **Line 974:** Added in `receive_radian_frame()` during frame reception
- Existing watchdog calls maintained in `cc1101_wait_for_packet()` and `get_meter_data()`

### Testing Recommendations
- Monitor serial output during wide initial scan (should complete without resets)
- Test manual frequency scan via MQTT (13 frequency steps)
- Verify no unexpected reboots during normal operation

---

## ✅ Fix #2: Stack Overflow Prevention (Recursion → Callbacks)

### Problem
`onUpdateData()` used recursive calls for retries, risking stack overflow after multiple failed attempts:
```cpp
// OLD - DANGEROUS:
delay(10000);
onUpdateData(); // Recursive call stacks up
```

### Solution
Replaced recursion with non-blocking callback scheduling using `mqtt.executeDelayed()`.

### Changes Made

#### `src/main.cpp` - Line 189
```cpp
// NEW - SAFE:
mqtt.executeDelayed(10000, onUpdateData); // Schedules callback without recursion
```

**Benefits:**
- No stack growth - each retry gets fresh stack space
- Non-blocking - doesn't freeze the system during 10-second waits
- Safer for long-term reliability

### Testing Recommendations
- Trigger multiple failed reads to test retry logic
- Monitor stack usage during retries (should remain constant)
- Verify system remains responsive during retry delays

---

## ✅ Fix #3: Buffer Overflow Protection

### Problem
`decode_4bitpbit_serial()` and `parse_meter_report()` had no bounds checking, allowing potential buffer overruns with malformed/corrupted RF data.

### Solution
Added comprehensive bounds checking with error logging.

### Changes Made

#### `src/cc1101.cpp` - `decode_4bitpbit_serial()`
- **Line 710:** Added `MAX_DECODED_SIZE` constant (200 bytes)
- **Line 749:** Added bounds check before writing each decoded byte
- **Line 757:** Added secondary bounds check before incrementing byte counter
- **Lines 750, 758:** Enhanced error messages with buffer position details

#### `src/cc1101.cpp` - `parse_meter_report()`
- **Line 773:** Initialize data structure to zeros
- **Line 776-779:** Validate minimum buffer size (30 bytes)
- **Line 781-785:** Fixed integer overflow using explicit `uint32_t` casts and bit shifts
- **Line 788:** Changed size check from `>= 48` to `>= 49` (need byte [48])
- **Line 795:** Added warning for undersized buffers

**Integer Overflow Fix:**
```cpp
// OLD - RISKY:
data.liters = decoded_buffer[18] + decoded_buffer[19] * 256 + ...

// NEW - SAFE:
data.liters = ((uint32_t)decoded_buffer[18]) |
              ((uint32_t)decoded_buffer[19] << 8) |
              ((uint32_t)decoded_buffer[20] << 16) |
              ((uint32_t)decoded_buffer[21] << 24);
```

### Testing Recommendations
- Test with corrupted RF data (if possible)
- Monitor serial output for "ERROR: Decode buffer overflow" messages
- Verify liters value handles large readings (> 16 million liters) correctly

---

## ✅ Fix #4: MQTT Command Input Validation

### Problem
MQTT subscription callbacks accepted any input without validation, creating security risks:
- Malicious commands could trigger actions
- Invalid inputs could cause unexpected behavior
- No logging of suspicious activity

### Solution
Implemented strict input whitelisting with logging of invalid attempts.

### Changes Made

#### `src/main.cpp` - `onConnectionEstablished()`

**Trigger Command (Lines 1245-1252):**
- ✅ Only accepts: `"update"` or `"read"`
- ✅ Logs warnings for invalid commands
- ✅ Publishes status message on rejection

**Restart Command (Lines 1268-1278):**
- ✅ Only accepts: `"restart"`
- ✅ Adds 2-second delay for MQTT message delivery
- ✅ Logs warnings for invalid commands

**Frequency Scan Command (Lines 1280-1289):**
- ✅ Only accepts: `"scan"`
- ✅ Logs warnings for invalid commands
- ✅ Publishes status message on rejection

### Security Benefits
- Prevents command injection attacks
- Creates audit trail of invalid attempts
- Provides user feedback via MQTT
- Follows principle of least privilege

### Testing Recommendations
```bash
# Valid commands (should work):
mosquitto_pub -t "everblu/cyble/trigger" -m "update"
mosquitto_pub -t "everblu/cyble/restart" -m "restart"
mosquitto_pub -t "everblu/cyble/frequency_scan" -m "scan"

# Invalid commands (should be rejected with warning):
mosquitto_pub -t "everblu/cyble/trigger" -m "hack"
mosquitto_pub -t "everblu/cyble/restart" -m "reboot"
mosquitto_pub -t "everblu/cyble/frequency_scan" -m "start"
```

---

## 📊 Impact Summary

| Fix | Severity | Impact | Files Modified |
|-----|----------|--------|----------------|
| Watchdog Protection | HIGH | Prevents random reboots | main.cpp, cc1101.cpp |
| Recursion → Callbacks | HIGH | Prevents stack overflow crashes | main.cpp |
| Buffer Overflow Protection | CRITICAL | Prevents memory corruption | cc1101.cpp |
| MQTT Input Validation | MEDIUM | Prevents malicious commands | main.cpp |

---

## 🧪 Compilation Status

**ESP8266 (Huzzah):**
✅ Compiles successfully (warnings are pre-existing and benign)

**ESP32 (esp32dev):**
✅ Compiles successfully

**Known Warnings (Non-Critical):**
- Sign comparison in loops (pre-existing)
- Unused static function declaration (pre-existing)

---

## 📝 Code Quality Improvements

In addition to fixing critical issues, these changes improved code quality:

1. **Better Error Messages:** Changed cryptic errors like "stop bit error10" to "ERROR: Stop bit error at bit 10"
2. **Consistent Logging:** All errors now start with "ERROR:" or "WARN:" prefix
3. **Code Comments:** Added explanatory comments for complex operations
4. **Defensive Programming:** Initialize data structures, validate inputs, check bounds

---

## 🚀 Next Steps (Optional - Not Critical)

These critical issues are now fixed. For further improvements, consider:

1. **Replace magic numbers** - Define constants for CC1101 register values
2. **Add configuration validation** - Validate METER_YEAR, METER_SERIAL at startup
3. **Reduce String usage** - Replace String with const char* where possible
4. **Add unit tests** - Test buffer overflow protection and input validation

---

## 🎯 Deployment Checklist

Before deploying these changes:

- [x] Review all code changes
- [ ] Build and upload to test device
- [ ] Monitor serial output during first boot
- [ ] Test meter reading functionality
- [ ] Test MQTT command validation
- [ ] Test frequency scanning
- [ ] Monitor for 24 hours to verify stability
- [ ] Deploy to production devices

---

## 📞 Support

If you encounter any issues after applying these fixes:

1. Check serial monitor for ERROR/WARN messages
2. Verify MQTT commands use correct syntax
3. Monitor device uptime (should be stable now)
4. Review MQTT logs for rejected commands

---

**Fixes Applied By:** GitHub Copilot  
**Reviewed By:** [Your Name]  
**Status:** ✅ Ready for Testing
