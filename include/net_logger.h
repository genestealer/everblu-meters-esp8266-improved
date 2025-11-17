#pragma once

#include <Arduino.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32)
  #include <WiFi.h>
#else
  #error "This logger is for ESP8266/ESP32 only"
#endif

// Configuration
#ifndef NETLOG_PORT
#define NETLOG_PORT 23        // Telnet-style logging port
#endif

// Call once after WiFi is connected
void initNetworkLogger();

// Call regularly from loop()
void handleNetworkLogger();

// Convenience logging helpers
void logPrint(const String &msg);
void logPrint(const char *msg);
void logPrintln(const String &msg);
void logPrintln(const char *msg);
void logPrintf(const char *fmt, ...);
