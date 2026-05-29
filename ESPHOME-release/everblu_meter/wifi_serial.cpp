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

            // Reset ring buffer so the new client starts from fresh output,
            // not stale data accumulated before it connected.
            _head = _tail = 0;
            _dropped = 0;
            _lastSendMs = millis(); // banner counts as first send; restart keepalive timer

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
#if defined(ESP8266)
            wifiSerialClient.printf("Reset reason: %s\n", ESP.getResetReason().c_str());
#endif
            wifiSerialClient.println("=====================================");
            wifiSerialClient.println();
        }
    }

    if (wifiSerialClient && !wifiSerialClient.connected())
    {
        _usb.println("[WiFi Serial] Client disconnected");
        wifiSerialClient.stop();
        _head = _tail = 0; // Discard buffered output for gone client
    }

    // Drain the async ring buffer to the TCP client.
    // Loop until the buffer is empty or TCP write returns 0 (send buffer full).
    // Using write()'s return value instead of availableForWrite(), which is
    // unreliable on ESP8266 WiFiClient.
    if (wifiSerialClient && wifiSerialClient.connected())
    {
        // Drain all pending bytes in one shot (multiple contiguous chunks)
        uint16_t pending = (_head - _tail) & (WIFI_SERIAL_TX_BUF_SIZE - 1);
        while (pending > 0)
        {
            uint16_t tailIdx = _tail & (WIFI_SERIAL_TX_BUF_SIZE - 1);
            // Use size_t to avoid uint16_t truncation when WIFI_SERIAL_TX_BUF_SIZE
            // equals 65536 and tailIdx is 0 (65536 wraps to 0 in uint16_t).
            size_t contiguous = WIFI_SERIAL_TX_BUF_SIZE - tailIdx;
            size_t toSend = (pending < contiguous) ? (size_t)pending : contiguous;
            size_t sent = wifiSerialClient.write(&_txBuf[tailIdx], toSend);
            if (sent == 0)
                break; // TCP send buffer full; retry next loop() call
            _tail += (uint16_t)sent;
            _lastSendMs = millis();
            pending = (_head - _tail) & (WIFI_SERIAL_TX_BUF_SIZE - 1);
        }

        // Keepalive: if no data has been sent for 20 seconds, send a small marker
        // line.  This prevents NAT routers from expiring the idle TCP session
        // (typical home/VLAN NAT idle timeout is 30-300s).
        if (millis() - _lastSendMs > 20000UL)
        {
            const char *ka = "# [WiFi Serial] alive\n";
            wifiSerialClient.print(ka);
            _lastSendMs = millis();
        }

        if (_dropped > 0)
        {
            // Report drops once the buffer drains enough to fit the notice
            if (_free() > 48)
            {
                char notice[48];
                snprintf(notice, sizeof(notice), "[WiFi Serial] %lu bytes dropped\n", (unsigned long)_dropped);
                for (size_t i = 0; i < strlen(notice); i++)
                    _enqueue((uint8_t)notice[i]);
                _dropped = 0;
            }
        }
    }
#endif
}

size_t WifiSerialStream::write(uint8_t c)
{
    _usb.write(c);
#if WIFI_SERIAL_HAS_WIFI
    // Non-blocking: enqueue into ring buffer; loop() drains to TCP client.
    if (wifiSerialClient && wifiSerialClient.connected())
    {
        _enqueue(c);
    }
#endif
    return 1;
}

size_t WifiSerialStream::write(const uint8_t *buffer, size_t size)
{
    _usb.write(buffer, size);
#if WIFI_SERIAL_HAS_WIFI
    // Non-blocking: enqueue into ring buffer; loop() drains to TCP client.
    if (wifiSerialClient && wifiSerialClient.connected())
    {
        for (size_t i = 0; i < size; i++)
            _enqueue(buffer[i]);
    }
#endif
    return size;
}

// Buffer size for printf formatting (including room for null terminator)
// Increased to 1024 bytes to accommodate large JSON history data and other verbose output
#define WIFI_SERIAL_PRINTF_BUFFER_SIZE 1024

size_t WifiSerialStream::printf(const char *format, ...)
{
    // Note: static to avoid stack pressure on ESP8266 (81920 bytes RAM total).
    // Safe here because WifiSerialStream is used exclusively from the main loop
    // on single-core, cooperative-scheduler platforms (ESP8266, ESP32 Arduino)
    // where no concurrent caller can re-enter this function before it returns.
    static char buffer[WIFI_SERIAL_PRINTF_BUFFER_SIZE];
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
    // Best-effort drain of pending bytes using write()'s return value.
    // Does not block waiting for the full buffer to empty.
#if WIFI_SERIAL_HAS_WIFI
    if (wifiSerialClient && wifiSerialClient.connected())
    {
        uint16_t pending = (_head - _tail) & (WIFI_SERIAL_TX_BUF_SIZE - 1);
        if (pending > 0)
        {
            uint16_t tailIdx = _tail & (WIFI_SERIAL_TX_BUF_SIZE - 1);
            // Use size_t to avoid uint16_t truncation when WIFI_SERIAL_TX_BUF_SIZE
            // equals 65536 and tailIdx is 0 (65536 wraps to 0 in uint16_t).
            size_t contiguous = WIFI_SERIAL_TX_BUF_SIZE - tailIdx;
            size_t toSend = (pending < contiguous) ? (size_t)pending : contiguous;
            size_t sent = wifiSerialClient.write(&_txBuf[tailIdx], toSend);
            _tail += (uint16_t)sent;
        }
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
