/**
 * @file SPI.h
 * @brief Minimal Arduino SPI shim for the PlatformIO `native` (host) build
 *
 * This header is NOT part of the firmware. It exists so that `src/core/cc1101.cpp`
 * can be compiled and unit-tested on a desktop host, where no SPI peripheral is
 * available. It is pulled in only by `[env:native_cc1101]`, via
 * `-I test/native_shims` in `platformio.ini`.
 *
 * Every byte the driver exchanges with the radio passes through
 * `SPI.transfer(buffer, length)`, so installing a handler here is enough to put a
 * simulated CC1101 (and any bus fault we want to reproduce) behind the whole
 * driver. See `test/test_native_cc1101_link/native_cc1101_device.h`.
 *
 * Scope is deliberately narrow: only the members `cc1101.cpp` actually calls.
 */

#ifndef EVERBLU_NATIVE_SPI_SHIM_H
#define EVERBLU_NATIVE_SPI_SHIM_H

#include <Arduino.h>

#ifndef MSBFIRST
#define MSBFIRST 1
#endif
#ifndef SPI_MODE0
#define SPI_MODE0 0
#endif

class SPISettings
{
public:
    SPISettings() = default;
    SPISettings(uint32_t, uint8_t, uint8_t) {}
};

/**
 * Handler invoked for every SPI transfer. The buffer is exchanged in place, the
 * same way real SPI works: on entry it holds the bytes clocked out on MOSI, and
 * on return it must hold the bytes clocked back in on MISO.
 */
using NativeSpiTransferHandler = void (*)(uint8_t *buffer, size_t length);

inline NativeSpiTransferHandler &nativeSpiHandler()
{
    static NativeSpiTransferHandler handler = nullptr;
    return handler;
}

inline void nativeSpiSetHandler(NativeSpiTransferHandler handler)
{
    nativeSpiHandler() = handler;
}

class NativeSPI
{
public:
    void begin() {}
    void begin(int, int, int, int) {}
    void pins(int, int, int, int) {}
    void beginTransaction(const SPISettings &) {}
    void endTransaction() {}

    void transfer(void *buffer, size_t length)
    {
        uint8_t *bytes = static_cast<uint8_t *>(buffer);
        if (nativeSpiHandler() != nullptr)
        {
            nativeSpiHandler()(bytes, length);
        }
        else
        {
            // No handler installed means nothing is on the bus, so MISO reads as
            // an undriven line rather than as plausible data.
            memset(bytes, 0xFF, length);
        }
    }

    uint8_t transfer(uint8_t out)
    {
        transfer(&out, 1);
        return out;
    }
};

inline NativeSPI SPI;

#endif // EVERBLU_NATIVE_SPI_SHIM_H
