/*
 * WiFi Serial Monitor
 * Provides TCP serial server that mirrors Serial output over WiFi
 */

#ifndef WIFI_SERIAL_H
#define WIFI_SERIAL_H

#include <Arduino.h>

// Default TCP port for WiFi serial (Telnet default)
#ifndef WIFI_SERIAL_PORT
#define WIFI_SERIAL_PORT 23
#endif

// Forward declaration of the combined USB+WiFi serial stream
class WifiSerialStream;
extern WifiSerialStream WiFiSerial;

/**
 * Combined USB + WiFi serial stream
 * Mirrors writes to both hardware Serial and the active WiFi client.
 */
class WifiSerialStream : public Print
{
public:
    // Use Stream& to support HardwareSerial, HWCDC (ESP32-S3 USB), and other Serial types
    explicit WifiSerialStream(Stream &usb) : _usb(usb) {}

    // Basic Serial-compatible API
    // Note: Stream base class doesn't have begin() or setDebugOutput()
    // These are provided as no-ops for compatibility
    void begin(unsigned long baud) { /* Stream doesn't have begin, ignored */ }
    void setDebugOutput(bool enable) { /* Stream doesn't have setDebugOutput, ignored */ }
    void flush();
    int available();
    int read();
    int peek();
    size_t printf(const char *format, ...) __attribute__((format(printf, 2, 3)));
    using Print::write;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    explicit operator bool() const { return true; }

    // Server lifecycle
    void beginServer();
    void loop();

private:
    Stream &_usb;
};

// C-style helpers retained for minimal integration
void wifiSerialBegin();
void wifiSerialLoop();
void wifiSerialPrint(const char *str);
void wifiSerialPrintln(const char *str);
void wifiSerialPrintf(const char *format, ...);

// Remap Serial to the combined stream for all translation units that include this header.
// NOTE:
// - This macro changes all uses of `Serial` to `WiFiSerial` in any file that includes this
//   header (unless WIFI_SERIAL_NO_REMAP is defined before including it).
// - WifiSerialStream only implements a subset of the HardwareSerial API. Third-party libraries
//   that rely on advanced Serial methods (e.g. availableForWrite(), readBytes(), etc.) may
//   not compile or may behave unexpectedly when this remap is active.
// - Known compatibility risk: any library expecting the full HardwareSerial surface (or relying
//   on Serial being a hardware UART) can fail to compile or behave differently. Prefer defining
//   WIFI_SERIAL_NO_REMAP and using WiFiSerial explicitly where needed.
// - To avoid remapping in code that expects the original HardwareSerial `Serial` object,
//   define WIFI_SERIAL_NO_REMAP before including this header, for example:
//   #define WIFI_SERIAL_NO_REMAP
//   #include "wifi_serial.h"
#ifndef WIFI_SERIAL_NO_REMAP
#define Serial WiFiSerial
#endif

#endif // WIFI_SERIAL_H
