# WiFi Serial Monitor

## Overview

This project includes a WiFi-based serial monitor that mirrors all `Serial.print` and `Serial.println` output over WiFi via a TCP server. This allows you to monitor your device remotely without a USB connection.

## Features

- **Non-blocking operation**: Does not interfere with the main loop or introduce delays
- **Automatic fallback**: Works seamlessly whether WiFi is connected or not
- **USB Serial preserved**: All USB Serial output continues to work normally
- **Single client support**: One WiFi client at a time (new clients replace existing ones)
- **Standard Telnet port**: Uses port 23 by default (configurable)

## Configuration

The TCP port can be customized by defining `WIFI_SERIAL_PORT` before including `wifi_serial.h`:

```cpp
#define WIFI_SERIAL_PORT 23  // Default: 23
#include "wifi_serial.h"
```

## Usage

### Connecting to WiFi Serial Monitor

Once your device is connected to WiFi and running, you can connect using any Telnet client:

**Windows (Command Prompt or PowerShell):**
```
telnet <device-ip> 23
```

**Linux/macOS:**
```
telnet <device-ip> 23
```

**PlatformIO Monitor (automatic):**

For OTA environments (`*-ota`), the `monitor_port` is already configured in `platformio.ini`:
```
pio run --target monitor --environment huzzah-ota
```

PlatformIO will automatically connect to the TCP socket instead of USB Serial.

### Example Connection

If your device IP is `192.168.2.21`:
```
telnet 192.168.2.21 23
```

You'll see:
```
=== WiFi Serial Monitor Connected ===
[followed by all Serial output]
```

## Implementation Details

### Files Added

- **`src/wifi_serial.h`**: Header file with function declarations
- **`src/wifi_serial.cpp`**: Implementation of the WiFi serial server

### Integration Points

1. **Initialization**: `wifiSerialBegin()` is called in `onConnectionEstablished()` after WiFi and OTA setup
2. **Main Loop**: `wifiSerialLoop()` is called in `loop()` to handle client connections (non-blocking)
3. **Output Functions**: 
   - `wifiSerialPrint(const char* str)`
   - `wifiSerialPrintln(const char* str)`
   - `wifiSerialPrintf(const char* format, ...)`

### Behavior

- **Before WiFi connects**: All output goes to USB Serial only
- **After WiFi connects**: TCP server starts and output goes to both USB Serial and any connected WiFi client
- **Client connects**: Existing client (if any) is disconnected, new client is accepted
- **Client disconnects**: Detected automatically, server continues to accept new connections
- **WiFi disconnects**: Server stops accepting connections, falls back to USB Serial only

## Troubleshooting

### Cannot connect to telnet

1. Verify device is connected to WiFi (check USB Serial output for IP address)
2. Ensure firewall allows telnet connections on port 23
3. Check that the IP address in `platformio.ini` matches your device's actual IP

### Output not appearing in telnet

1. Verify you're connected (you should see the welcome message)
2. Check that the application is actually printing to Serial
3. Try reconnecting (disconnect and connect again)

### USB Serial stops working

This should not happen - USB Serial output is always preserved. If you experience issues:
1. Check USB cable and connection
2. Verify correct baud rate (115200)
3. Try resetting the device

## Technical Notes

- **TCP No-Delay**: Nagle algorithm is disabled for lower latency
- **Buffer Management**: Uses WiFiClient internal buffering (no additional buffers)
- **Memory Impact**: Minimal - only one WiFiServer and one WiFiClient object
- **Performance**: Non-blocking calls ensure no impact on main application timing
