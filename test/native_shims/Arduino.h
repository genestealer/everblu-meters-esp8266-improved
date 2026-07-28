/**
 * @file Arduino.h
 * @brief Minimal Arduino compatibility shim for the PlatformIO `native` (host) build
 *
 * This header is NOT part of the firmware. It exists so that platform-neutral
 * service code (for example `src/services/schedule_manager.cpp` and the MQTT
 * logging path in `src/core/logging.h`) can be compiled and unit-tested on a
 * desktop host, where no Arduino core is available.
 *
 * It is pulled in only by the `[env:native]` environment, via
 * `-I test/native_shims` in `platformio.ini`. Hardware builds keep using the
 * real Arduino core header.
 *
 * Scope is deliberately narrow: only the symbols actually referenced by the
 * host-tested sources are provided. Add to it when a newly host-tested source
 * needs something, rather than trying to emulate the whole Arduino API.
 */

#ifndef EVERBLU_NATIVE_ARDUINO_SHIM_H
#define EVERBLU_NATIVE_ARDUINO_SHIM_H

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

// The real Arduino core exposes the C maths library (NAN, isnan, roundf, fabs)
// and the C time functions in the global namespace, and shared service code
// relies on both.
#include <math.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
//
// The host clock is virtual and starts at zero. Tests drive it explicitly with
// nativeClockAdvance(), so timing-dependent code (retry delays, cooldowns,
// scan step pacing) is exercised deterministically and instantly instead of
// sleeping. delay() advances it, which mirrors what the firmware experiences.

inline unsigned long &nativeClockMillisRef()
{
    static unsigned long ms = 0;
    return ms;
}

inline void nativeClockSet(unsigned long ms) { nativeClockMillisRef() = ms; }
inline void nativeClockAdvance(unsigned long ms) { nativeClockMillisRef() += ms; }
inline void nativeClockReset() { nativeClockMillisRef() = 0; }

inline unsigned long millis() { return nativeClockMillisRef(); }
inline unsigned long micros() { return nativeClockMillisRef() * 1000UL; }

// Unit tests must never actually sleep: advance the virtual clock instead.
inline void delay(unsigned long ms) { nativeClockAdvance(ms); }
inline void delayMicroseconds(unsigned int us) { nativeClockAdvance(us / 1000UL); }
inline void yield() {}

// ---------------------------------------------------------------------------
// Arduino maths helpers
// ---------------------------------------------------------------------------

#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    if (in_max == in_min)
    {
        return out_min;
    }
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// ---------------------------------------------------------------------------
// Print / Stream
// ---------------------------------------------------------------------------
//
// src/core/wifi_serial.h derives WifiSerialStream from Print and holds a
// Stream reference, so both base classes have to exist for that header to
// compile on the host.

class Print
{
public:
    virtual ~Print() = default;

    virtual size_t write(uint8_t c) = 0;

    virtual size_t write(const uint8_t *buffer, size_t size)
    {
        size_t written = 0;
        for (size_t i = 0; i < size; i++)
        {
            written += write(buffer[i]);
        }
        return written;
    }

    size_t write(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }

    size_t print(char c) { return write((uint8_t)c); }
    size_t print(const char *s) { return write(s); }
    size_t print(const std::string &s) { return write(s.c_str()); }

    size_t println() { return write((uint8_t)'\n'); }
    size_t println(const char *s) { return print(s) + println(); }
    size_t println(const std::string &s) { return println(s.c_str()); }

    size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
    {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        const int n = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n <= 0)
        {
            return 0;
        }
        const size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        return write((const uint8_t *)buf, len);
    }
};

class Stream : public Print
{
public:
    virtual int available() { return 0; }
    virtual int read() { return -1; }
    virtual int peek() { return -1; }
    virtual void flush() {}
};

// ---------------------------------------------------------------------------
// Serial
// ---------------------------------------------------------------------------
//
// Output is discarded by default so that test runs stay readable. Set the
// environment variable EVERBLU_NATIVE_SERIAL=1 to mirror firmware log output to
// stdout when debugging a failing test.

class NativeSerial : public Stream
{
public:
    void begin(unsigned long = 0) {}
    void setDebugOutput(bool) {}
    void flush() override { fflush(stdout); }
    explicit operator bool() const { return true; }

    size_t write(uint8_t c) override
    {
        if (enabled())
        {
            fputc((int)c, stdout);
        }
        return 1;
    }

    static bool enabled()
    {
        static const bool on = (std::getenv("EVERBLU_NATIVE_SERIAL") != nullptr);
        return on;
    }
};

inline NativeSerial Serial;

// ---------------------------------------------------------------------------
// Assorted Arduino spellings used by shared code
// ---------------------------------------------------------------------------

using String = std::string;

#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif

#endif // EVERBLU_NATIVE_ARDUINO_SHIM_H
