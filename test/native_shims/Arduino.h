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

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

inline unsigned long millis()
{
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return (unsigned long)duration_cast<milliseconds>(steady_clock::now() - start).count();
}

inline unsigned long micros()
{
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return (unsigned long)duration_cast<microseconds>(steady_clock::now() - start).count();
}

// No-ops on the host: unit tests must never actually sleep.
inline void delay(unsigned long) {}
inline void delayMicroseconds(unsigned int) {}
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
// Serial
// ---------------------------------------------------------------------------
//
// Output is discarded by default so that test runs stay readable. Set the
// environment variable EVERBLU_NATIVE_SERIAL=1 to mirror firmware log output to
// stdout when debugging a failing test.

class NativeSerial
{
public:
    void begin(unsigned long = 0) {}
    void flush() {}
    explicit operator bool() const { return true; }

    int printf(const char *fmt, ...)
    {
        if (!enabled())
        {
            return 0;
        }
        va_list args;
        va_start(args, fmt);
        const int written = vfprintf(stdout, fmt, args);
        va_end(args);
        return written;
    }

    void print(const char *s)
    {
        if (enabled() && s)
        {
            fputs(s, stdout);
        }
    }

    void print(const std::string &s) { print(s.c_str()); }

    void println(const char *s)
    {
        if (enabled())
        {
            fputs(s ? s : "", stdout);
            fputc('\n', stdout);
        }
    }

    void println(const std::string &s) { println(s.c_str()); }
    void println() { println(""); }

private:
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
