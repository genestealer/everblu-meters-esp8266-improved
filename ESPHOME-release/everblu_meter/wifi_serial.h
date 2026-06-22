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
// Async transmit ring-buffer size (bytes).  Must be a power of two.
// 8192 gives ~8 KB of headroom to absorb a full meter read sequence
// (WUP + RX + hex dump + 12-month history ≈ 3.5 KB) while loop() is
// not being called during the CC1101 TX/RX phase.
// Lowering saves RAM at the cost of more frequent drop events.
#ifndef WIFI_SERIAL_TX_BUF_SIZE
#define WIFI_SERIAL_TX_BUF_SIZE 8192
#endif

class WifiSerialStream : public Print
{
    // Compile-time guards: ring logic requires a power-of-two size and 16-bit counters.
    static_assert((WIFI_SERIAL_TX_BUF_SIZE & (WIFI_SERIAL_TX_BUF_SIZE - 1)) == 0,
                  "WIFI_SERIAL_TX_BUF_SIZE must be a power of two");
    static_assert(WIFI_SERIAL_TX_BUF_SIZE <= 65536U,
                  "WIFI_SERIAL_TX_BUF_SIZE must fit within uint16_t arithmetic (max 65536)");

public:
    // Use Stream& to support HardwareSerial, HWCDC (ESP32-S3 USB), and other Serial types
    explicit WifiSerialStream(Stream &usb) : _usb(usb), _head(0), _tail(0), _dropped(0), _lastSendMs(0) {}

    // Basic Serial-compatible API
    // Forward to the real global Serial so remapped Serial.begin() still
    // initializes the underlying USB/UART serial device.
    void begin(unsigned long baud) { ::Serial.begin(baud); }
    void setDebugOutput(bool enable) { ::Serial.setDebugOutput(enable); }
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

    // Async ring buffer for WiFi TX (never blocks the caller)
    uint8_t _txBuf[WIFI_SERIAL_TX_BUF_SIZE];
    volatile uint16_t _head;   // write index (producer)
    volatile uint16_t _tail;   // read  index (consumer, drained in loop())
    uint32_t _dropped;         // bytes silently dropped when buffer was full
    unsigned long _lastSendMs; // millis() when data was last sent to client

    // Returns number of free bytes in the ring buffer
    inline uint16_t _free() const
    {
        // Cast the head/tail delta to uint16_t before masking: the subtraction is
        // otherwise promoted to signed int and can be negative after wraparound,
        // which would make the bitwise AND rely on implementation-defined behaviour.
        return (WIFI_SERIAL_TX_BUF_SIZE - 1) -
               ((uint16_t)(_head - _tail) & (WIFI_SERIAL_TX_BUF_SIZE - 1));
    }

    // Enqueue one byte; returns false and increments _dropped if full
    inline bool _enqueue(uint8_t c)
    {
        if (_free() == 0)
        {
            _dropped++;
            return false;
        }
        _txBuf[_head & (WIFI_SERIAL_TX_BUF_SIZE - 1)] = c;
        _head++;
        return true;
    }
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
