#include "net_logger.h"
#include <stdarg.h>

static WiFiServer netlogServer(NETLOG_PORT);
static WiFiClient netlogClient;

// Internal: write raw bytes to Serial and client
static void netlogWrite(const uint8_t *buf, size_t len) {
  // Always write to Serial for local debugging
  Serial.write(buf, len);

  // If we have an active client, mirror the data
  if (netlogClient && netlogClient.connected()) {
    netlogClient.write(buf, len);
  }
}

void initNetworkLogger() {
  // Start the TCP server only after WiFi is up
  netlogServer.begin();
  netlogServer.setNoDelay(true);

  const String msg =
      "\r\n[netlog] TCP logger listening on port " + String(NETLOG_PORT) +
      " (" + WiFi.localIP().toString() + ")\r\n";
  netlogWrite(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.length());
}

// Accept/maintain single client, handle disconnects
void handleNetworkLogger() {
  // Drop client if its gone
  if (netlogClient && !netlogClient.connected()) {
    netlogClient.stop();
  }

  // Accept a new client if we don't have one
  if (!netlogClient || !netlogClient.connected()) {
    WiFiClient incoming = netlogServer.available();
    if (incoming) {
      // If there was an old client, close it
      if (netlogClient) {
        netlogClient.stop();
      }
      netlogClient = incoming;

      const String banner =
          "\r\n[netlog] Client connected from " +
          netlogClient.remoteIP().toString() + "\r\n";
      netlogWrite(reinterpret_cast<const uint8_t*>(banner.c_str()), banner.length());
    }
  }

  // Optional: discard any data the client sends (were log-only)
  if (netlogClient && netlogClient.connected() && netlogClient.available()) {
    while (netlogClient.available()) {
      (void)netlogClient.read();
    }
  }
}

// ---------- Public helpers ----------

void logPrint(const String &msg) {
  netlogWrite(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.length());
}

void logPrint(const char *msg) {
  if (!msg) return;
  netlogWrite(reinterpret_cast<const uint8_t*>(msg), strlen(msg));
}

void logPrintln(const String &msg) {
  String line = msg;
  line += "\r\n";
  netlogWrite(reinterpret_cast<const uint8_t*>(line.c_str()), line.length());
}

void logPrintln(const char *msg) {
  if (!msg) {
    const char crlf[] = "\r\n";
    netlogWrite(reinterpret_cast<const uint8_t*>(crlf), 2);
    return;
  }
  String line(msg);
  line += "\r\n";
  netlogWrite(reinterpret_cast<const uint8_t*>(line.c_str()), line.length());
}

void logPrintf(const char *fmt, ...) {
  if (!fmt) return;

  char buf[256];  // small, fixed buffer: adjust if you need longer lines
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (n <= 0) return;

  // Clamp length to buffer size
  size_t len = (n < (int)sizeof(buf)) ? (size_t)n : sizeof(buf) - 1;
  netlogWrite(reinterpret_cast<const uint8_t*>(buf), len);
}
