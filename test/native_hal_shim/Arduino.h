/**
 * @file Arduino.h  (NATIVE HAL TEST SHIM - not the real Arduino core)
 *
 * Minimal host-side replacement for the Arduino core, used ONLY by the
 * `native_hal` PlatformIO environment to compile the real firmware
 * translation units (src/core/cc1101.cpp, utils.cpp, ...) on the host so a
 * complete meter read can be simulated against a fake CC1101 chip.
 *
 * The GPIO / timing / Serial primitives declared here are IMPLEMENTED in
 * test/test_native_hal_meter_read/fake_cc1101.cpp so they can drive and observe
 * the simulated chip and virtual clock.
 *
 * This file is deliberately tiny: it provides just enough of the Arduino
 * surface that the shared firmware code references. It is never shipped.
 */
#ifndef NATIVE_HAL_ARDUINO_SHIM_H
#define NATIVE_HAL_ARDUINO_SHIM_H

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <time.h> // utils.cpp print_time() uses time()/localtime()/strftime()

// -- Basic Arduino typedefs --------------------------------------------------
typedef uint8_t byte;
typedef bool boolean;
typedef uint16_t word;

// -- Digital pin / logic constants ------------------------------------------
#ifndef HIGH
#define HIGH 0x1
#endif
#ifndef LOW
#define LOW 0x0
#endif
#ifndef INPUT
#define INPUT 0x0
#endif
#ifndef OUTPUT
#define OUTPUT 0x1
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 0x2
#endif
#ifndef MSBFIRST
#define MSBFIRST 1
#endif
#ifndef LSBFIRST
#define LSBFIRST 0
#endif

// -- Arduino helper macros ---------------------------------------------------
// NOTE: intentionally NOT defining min()/max() macros here - they collide with
// the C++ standard library templates pulled in by host headers.
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

static inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
  if (in_max == in_min)
    return out_min;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// -- GPIO / timing primitives (implemented in fake_cc1101.cpp) ---------------
#ifdef __cplusplus
extern "C"
{
#endif
  void pinMode(uint8_t pin, uint8_t mode);
  void digitalWrite(uint8_t pin, uint8_t val);
  int digitalRead(uint8_t pin);
  void delay(unsigned long ms);
  void delayMicroseconds(unsigned int us);
  unsigned long millis(void);
  unsigned long micros(void);
  void yield(void);
#ifdef __cplusplus
}
#endif

// -- Print / Stream (subset used by logging.h and wifi_serial.h) -------------
class Print
{
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t *buffer, size_t size)
  {
    size_t n = 0;
    while (size--)
      n += write(*buffer++);
    return n;
  }
  size_t print(const char *s)
  {
    if (!s)
      return 0;
    return write(reinterpret_cast<const uint8_t *>(s), strlen(s));
  }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t println(const char *s)
  {
    size_t n = print(s);
    n += print('\n');
    return n;
  }
  size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
  {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
      return 0;
    size_t len = (static_cast<size_t>(n) < sizeof(buf)) ? static_cast<size_t>(n) : sizeof(buf) - 1;
    return write(reinterpret_cast<const uint8_t *>(buf), len);
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

// Hardware serial stand-in: writes to the host stdout so simulation logs are
// visible when running the test with `-v`.
class HardwareSerial : public Stream
{
public:
  void begin(unsigned long) {}
  void setDebugOutput(bool) {}
  size_t write(uint8_t c) override
  {
    putchar(static_cast<int>(c));
    return 1;
  }
  using Print::write;
};

extern HardwareSerial Serial;

#endif // NATIVE_HAL_ARDUINO_SHIM_H
