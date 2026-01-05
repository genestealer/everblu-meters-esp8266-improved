/*
 * WiFi Serial Monitor Implementation
 * Provides TCP serial server that mirrors Serial output over WiFi
 */

#define WIFI_SERIAL_NO_REMAP
#include "wifi_serial.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif

// TCP server on configured port
static WiFiServer *wifiSerialServer = nullptr;
static WiFiClient wifiSerialClient;
static bool serverStarted = false;

// Global combined stream instance
WifiSerialStream WiFiSerial(::Serial);

// ------------------------------
// WifiSerialStream methods
// ------------------------------

void WifiSerialStream::beginServer()
{
    if (WiFi.status() != WL_CONNECTED || serverStarted)
    {
        return;
    }

    wifiSerialServer = new WiFiServer(WIFI_SERIAL_PORT);
    wifiSerialServer->begin();
    wifiSerialServer->setNoDelay(true); // Lower latency for logs
    serverStarted = true;

    _usb.printf("[WiFi Serial] Server started on port %d\n", WIFI_SERIAL_PORT);
    _usb.printf("[WiFi Serial] Connect using: telnet %s %d\n",
                WiFi.localIP().toString().c_str(), WIFI_SERIAL_PORT);
}

void WifiSerialStream::loop()
{
    if (!serverStarted || WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    if (wifiSerialServer->hasClient())
    {
        if (wifiSerialClient && wifiSerialClient.connected())
        {
            _usb.println("[WiFi Serial] New client connecting - disconnecting existing client");
            wifiSerialClient.stop();
        }

        wifiSerialClient = wifiSerialServer->accept();
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
}

size_t WifiSerialStream::write(uint8_t c)
{
    _usb.write(c);
    if (wifiSerialClient && wifiSerialClient.connected())
    {
        wifiSerialClient.write(c);
    }
    return 1;
}

size_t WifiSerialStream::write(const uint8_t *buffer, size_t size)
{
    _usb.write(buffer, size);
    if (wifiSerialClient && wifiSerialClient.connected())
    {
        wifiSerialClient.write(buffer, size);
    }
    return size;
}

size_t WifiSerialStream::printf(const char *format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return write(reinterpret_cast<uint8_t *>(buffer), strlen(buffer));
}

void WifiSerialStream::flush()
{
    _usb.flush();
    if (wifiSerialClient && wifiSerialClient.connected())
    {
        wifiSerialClient.flush();
    }
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
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    WiFiSerial.print(buffer);
}
