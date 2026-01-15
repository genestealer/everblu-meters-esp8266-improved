# Logging System

## Overview

The firmware implements a unified logging system that works consistently across both MQTT standalone mode and ESPHome integration mode. This ensures logs are visible whether connected via USB serial or WiFi.

## Architecture

### Cross-Platform Logging Macros (`src/core/logging.h`)

```cpp
LOG_D(tag, format, ...)  // Debug level
LOG_I(tag, format, ...)  // Info level  
LOG_W(tag, format, ...)  // Warning level
LOG_E(tag, format, ...)  // Error level
```

### Mode-Specific Routing

**ESPHome Mode** (`USE_ESPHOME` defined):
- Routes to `ESP_LOGD()`, `ESP_LOGI()`, `ESP_LOGW()`, `ESP_LOGE()`
- Logs captured by ESPHome logger infrastructure
- **Visible over WiFi** in ESPHome web interface and Home Assistant
- Also visible via USB serial

**MQTT Standalone Mode** (`USE_ESPHOME` not defined):
- Routes to `Serial.printf()` with consistent formatting: `[Level][Tag] message`
- Only visible via USB serial connection
- Minimal overhead for time-critical operations

## Usage Examples

```cpp
#include "core/logging.h"

// Info level log
LOG_I("MeterReader", "Starting meter read sequence");

// With formatting
LOG_I("MeterReader", "Read attempt %d/%d", retryCount, maxRetries);

// Warning
LOG_W("CC1101", "Signal quality low: %d%%", signalPercent);

// Error
LOG_E("Storage", "Failed to save offset: %f MHz", offset);
```

## File-by-File Implementation Status

### ✅ Using Unified Logging

- **`src/core/utils.cpp`** - `printMeterDataSummary()`, signal quality functions
- **`src/services/meter_reader.cpp`** - All orchestration and status messages
- **`src/services/meter_history.cpp`** - Historical data printing

### ⚠️ Still Using Direct Serial (Time-Critical)

- **`src/core/cc1101.cpp`** - RF protocol implementation
  - **Reason**: Time-critical RF operations require minimal latency
  - ESPHome logger operations can add 10-3000ms blocking delay
  - Would cause frame corruption and missed receptions
  - **Status**: Intentionally kept as direct `Serial.print()` / `printf()`

### 🔄 Legacy Serial (MQTT Mode Only)

- **`src/main.cpp`** - Uses direct `Serial.printf()` throughout
  - Only runs in MQTT mode (never compiled for ESPHome)
  - No need to convert since it's mode-specific

## Why This Matters

### Problem: WiFi Logs Were Invisible

**Before:**
```
// USB connection
[15:21:43][METER] Starting meter read...
[15:21:43]=== METER DATA ===
[15:21:43][METER DATA] Volume (L): 740658
...

// WiFi connection
[15:21:43][D][button:022]: 'Read Meter Now' Pressed.
[15:21:43][I][everblu_meter:259]: Manual read requested via button
// ❌ All meter data logs missing!
```

**After:**
```
// USB and WiFi both show:
[15:21:43][D][button:022]: 'Read Meter Now' Pressed.
[15:21:43][I][everblu_meter:259]: Manual read requested via button
[15:21:43][I][MeterReader] Starting meter read...
[15:21:43][I][METER] === METER DATA ===
[15:21:43][I][METER] Volume (L): 740658
...
```

### Technical Details

**Direct Serial.print():**
- Writes directly to UART hardware buffer
- Not captured by ESPHome logger
- Only visible if physically connected via USB
- Zero overhead for time-critical code

**ESPHome Logger (ESP_LOG* macros):**
- Buffered, networked logging system
- Transmitted over WiFi to Home Assistant/web interface
- Can block for 10-3000ms during network transmission
- Perfect for status updates, unsuitable for RF timing

## Performance Characteristics

| Operation Type | Direct Serial | ESPHome Logger | Recommendation |
|----------------|---------------|----------------|----------------|
| Status updates | ✓ Fast | ✓ Acceptable | Use LOG_* |
| Meter summaries | ✓ Fast | ✓ Acceptable | Use LOG_* |
| Historical data | ✓ Fast | ✓ Acceptable | Use LOG_* |
| RF protocol | ✓ Critical | ❌ Too slow | Keep Serial |
| SPI timing | ✓ Critical | ❌ Too slow | Keep Serial |

## Migration Guide

### When to Convert

✅ **Convert to LOG_* macros:**
- Status messages
- Configuration info
- Success/failure notifications  
- User-facing summaries
- Debugging info (non-critical)

❌ **Keep as Serial.print():**
- Time-critical loops
- RF transmission/reception
- SPI communication
- Interrupt handlers
- Sub-millisecond timing requirements

### Conversion Example

```cpp
// Before
Serial.printf("[MeterReader] Read attempt %d/%d\n", retry, maxRetries);

// After
LOG_I("MeterReader", "Read attempt %d/%d", retry, maxRetries);
```

**Note:** Remove `\n` from format strings - the macros add it automatically.

## Build Impact

**Memory Usage:**
- Minimal - macros compile to appropriate backend
- No runtime overhead in MQTT mode (direct Serial)
- Slight overhead in ESPHome mode (logger buffer)

**Firmware Size:**
- Same for MQTT mode (compiles to same Serial.printf)
- +~2KB for ESPHome mode (logger infrastructure)

## Troubleshooting

### Logs Still Missing Over WiFi

1. Verify ESPHome logger level in YAML:
```yaml
logger:
  level: DEBUG  # or INFO, VERBOSE, etc.
```

2. Check component is using LOG_* macros:
```bash
grep -r "Serial.print" src/services/  # Should be minimal
grep -r "LOG_I" src/services/         # Should find many
```

3. Ensure `logging.h` is included:
```cpp
#include "core/logging.h"
```

### Logs Appearing Twice

If you see duplicate logs, check for mixed usage:
```cpp
// Wrong - will duplicate
Serial.println("Starting read");
LOG_I("Tag", "Starting read");

// Correct - choose one
LOG_I("Tag", "Starting read");
```

## Future Improvements

- [ ] Add log level configuration via MQTT/ESPHome
- [ ] Implement log buffering for RF-critical sections
- [ ] Add optional timestamp prefixes
- [ ] Convert remaining non-critical Serial.print() calls

## References

- ESPHome Logger: https://esphome.io/components/logger.html
- Arduino Serial: https://www.arduino.cc/reference/en/language/functions/communication/serial/
