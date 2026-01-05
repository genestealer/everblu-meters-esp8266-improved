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
    explicit WifiSerialStream(HardwareSerial &usb) : _usb(usb) {}

    // Basic Serial-compatible API
    void begin(unsigned long baud) { _usb.begin(baud); }
    void setDebugOutput(bool enable) { _usb.setDebugOutput(enable); }
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
    HardwareSerial &_usb;
};

// C-style helpers retained for minimal integration
void wifiSerialBegin();
void wifiSerialLoop();
void wifiSerialPrint(const char *str);
void wifiSerialPrintln(const char *str);
void wifiSerialPrintf(const char *format, ...);

// Remap Serial to the combined stream for all translation units that include this header
#ifndef WIFI_SERIAL_NO_REMAP
#define Serial WiFiSerial
#endif

#endif // WIFI_SERIAL_H
