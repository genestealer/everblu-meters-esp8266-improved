/**
 * @file SPI.h  (NATIVE HAL TEST SHIM - not the real Arduino SPI library)
 *
 * Minimal host-side SPI replacement for the `native_hal` PlatformIO
 * environment. `SPI.transfer(buf, len)` forwards the full-duplex exchange to
 * the simulated CC1101 chip (see fake_cc1101.cpp), which fills `buf` with the
 * bytes the real chip would clock back on MISO.
 */
#ifndef NATIVE_HAL_SPI_SHIM_H
#define NATIVE_HAL_SPI_SHIM_H

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

#ifndef SPI_MODE0
#define SPI_MODE0 0x00
#endif

class SPISettings
{
public:
  SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode)
      : clock_(clock), bitOrder_(bitOrder), dataMode_(dataMode) {}
  SPISettings() : clock_(1000000), bitOrder_(MSBFIRST), dataMode_(SPI_MODE0) {}
  uint32_t clock_;
  uint8_t bitOrder_;
  uint8_t dataMode_;
};

class SPIClass
{
public:
  void begin() {}
  void begin(int8_t, int8_t, int8_t, int8_t) {}
  void pins(int8_t, int8_t, int8_t, int8_t) {}
  void end() {}
  void beginTransaction(SPISettings) {}
  void endTransaction() {}

  // Single-byte and buffer transfers route to the simulated chip.
  uint8_t transfer(uint8_t data);
  void transfer(void *buf, size_t count);
};

extern SPIClass SPI;

#endif // NATIVE_HAL_SPI_SHIM_H
