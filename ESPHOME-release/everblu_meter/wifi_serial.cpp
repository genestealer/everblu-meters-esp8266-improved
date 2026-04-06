/*
 * WiFi Serial Monitor Implementation
 * Provides TCP serial server that mirrors Serial output over WiFi
 */

#ifndef WIFI_SERIAL_NO_REMAP
#define WIFI_SERIAL_NO_REMAP
#endif
#include "wifi_serial.h"
#include "version.h"

#if __has_include(<ESP8266WiFi.h>)
#include <ESP8266WiFi.h>
#define WIFI_SERIAL_HAS_WIFI 1
#elif __has_include(<WiFi.h>)
#include <WiFi.h>
#define WIFI_SERIAL_HAS_WIFI 1
#else
#define WIFI_SERIAL_HAS_WIFI 0
#endif

// TCP server on configured port
#if WIFI_SERIAL_HAS_WIFI
static WiFiServer wifiSerialServer(WIFI_SERIAL_PORT);
static WiFiClient wifiSerialClient;
#endif
static bool serverStarted = false;

// Global combined stream instance
WifiSerialStream WiFiSerial(::Serial);

// ------------------------------
// WifiSerialStream methods
// ------------------------------

void WifiSerialStream::beginServer()
{
#if WIFI_SERIAL_HAS_WIFI
    if (WiFi.status() != WL_CONNECTED || serverStarted)
    {
        return;
    }

    // Start the server on the statically allocated WiFiServer instance.
    wifiSerialServer.begin();
    wifiSerialServer.setNoDelay(true); // Lower latency for logs
    serverStarted = true;

    _usb.printf("[WiFi Serial] Server started on port %d\n", WIFI_SERIAL_PORT);
    _usb.printf("[WiFi Serial] Connect using: telnet %s %d\n",
                WiFi.localIP().toString().c_str(), WIFI_SERIAL_PORT);
    // NOTE: This server is unauthenticated and unencrypted. Any device on your local network
    // can connect and view serial output, which may include WiFi/MQTT credentials and internal state.
#else
    if (!serverStarted)
    {
        _usb.println("[WiFi Serial] WiFi headers unavailable in this build; TCP serial disabled");
        serverStarted = true;
    }
#endif
}

void WifiSerialStream::loop()
{
#if WIFI_SERIAL_HAS_WIFI
    if (!serverStarted || WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    if (wifiSerialServer.hasClient())
    {
        if (wifiSerialClient && wifiSerialClient.connected())
        {
            _usb.println("[WiFi Serial] New client connecting - disconnecting existing client");
            wifiSerialClient.stop();
        }

        wifiSerialClient = wifiSerialServer.accept();
        if (wifiSerialClient)
        {
            _usb.printf("[WiFi Serial] Client connected from %s\n",
                        wifiSerialClient.remoteIP().toString().c_str());
            wifiSerialClient.setNoDelay(true);

            // Send welcome banner to new client
            wifiSerialClient.println("\n=====================================");
            wifiSerialClient.println("WiFi Serial Monitor Connected");
            wifiSerialClient.println("=====================================");
            wifiSerialClient.println("Everblu Meters ESP8266/ESP32");
            wifiSerialClient.printf("Firmware Version: %s\n", EVERBLU_FW_VERSION);
            wifiSerialClient.println("Water/Gas usage data for Home Assistant");
            wifiSerialClient.println("https://github.com/genestealer/everblu-meters-esp8266-improved");
            wifiSerialClient.println();
            wifiSerialClient.printf("Device IP: %s\n", WiFi.localIP().toString().c_str());
            wifiSerialClient.printf("WiFi SSID: %s\n", WiFi.SSID().c_str());
            wifiSerialClient.printf("WiFi RSSI: %d dBm\n", WiFi.RSSI());
            wifiSerialClient.printf("Uptime: %lu seconds\n", millis() / 1000);
            wifiSerialClient.println("=====================================");
            wifiSerialClient.println();
        }
    }

    if (wifiSerialClient && !wifiSerialClient.connected())
    {
        _usb.println("[WiFi Serial] Client disconnected");
        wifiSerialClient.stop();
    }
#endif
}

size_t WifiSerialStream::write(uint8_t c)
{
    _usb.write(c);
    // NOTE: WiFi client write calls may block if the TCP buffer is full or connection is slow.
    // This could briefly block the main application loop.
#if WIFI_SERIAL_HAS_WIFI
    if (wifiSerialClient && wifiSerialClient.connected())
    {
        wifiSerialClient.write(c);
    }
#endif
    return 1;
}

size_t WifiSerialStream::write(const uint8_t *buffer, size_t size)
{
    _usb.write(buffer, size);
    // NOTE: WiFi client write calls may block if the TCP buffer is full or connection is slow.
    // This could briefly block the main application loop.
#if WIFI_SERIAL_HAS_WIFI
    if (wifiSerialClient && wifiSerialClient.connected())
    {
        wifiSerialClient.write(buffer, size);
    }
#endif
    return size;
}

// Buffer size for printf formatting (including room for null terminator)
// Increased to 1024 bytes to accommodate large JSON history data and other verbose output
#define WIFI_SERIAL_PRINTF_BUFFER_SIZE 1024

size_t WifiSerialStream::printf(const char *format, ...)
{
    char buffer[WIFI_SERIAL_PRINTF_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    bool truncated = (len < 0) || (len >= static_cast<int>(sizeof(buffer)));
    if (truncated)
    {
        _usb.printf("[WiFi Serial] Warning: printf output truncated (buffer %d bytes)\n", WIFI_SERIAL_PRINTF_BUFFER_SIZE);
    }

    return write(reinterpret_cast<uint8_t *>(buffer), strlen(buffer));
}

void WifiSerialStream::flush()
{
    _usb.flush();
#if WIFI_SERIAL_HAS_WIFI
    if (wifiSerialClient && wifiSerialClient.connected())
    {
        wifiSerialClient.flush();
    }
#endif
}

int WifiSerialStream::available()
{
    return _usb.available();
}

int WifiSerialStream::read()
{
    return _usb.read();
}

int WifiSerialStream::peek()
{
    return _usb.peek();
}

// ------------------------------
// C-style helpers for existing code
// ------------------------------

void wifiSerialBegin()
{
    WiFiSerial.beginServer();
}

void wifiSerialLoop()
{
    WiFiSerial.loop();
}

void wifiSerialPrint(const char *str)
{
    WiFiSerial.print(str);
}

void wifiSerialPrintln(const char *str)
{
    WiFiSerial.println(str);
}

void wifiSerialPrintf(const char *format, ...)
{
    char buffer[WIFI_SERIAL_PRINTF_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    bool truncated = (len < 0) || (len >= static_cast<int>(sizeof(buffer)));
    if (truncated)
    {
        WiFiSerial.printf("[WiFi Serial] Warning: printf output truncated (buffer %d bytes)\n", WIFI_SERIAL_PRINTF_BUFFER_SIZE);
    }

    WiFiSerial.print(buffer);
}
