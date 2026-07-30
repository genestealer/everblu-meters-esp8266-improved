/**
 * @file native_cc1101_device.h
 * @brief A simulated CC1101 on the host SPI bus, with bus-fault injection
 *
 * `src/core/cc1101.cpp` reaches the radio through exactly one function,
 * `wiringPiSPIDataRW()`, which on the host resolves to `SPI.transfer()` in
 * `test/native_shims/SPI.h`. Installing this device as the transfer handler
 * therefore puts a whole simulated radio behind the real driver, unmodified.
 *
 * The device models a healthy CC1101 register file. Faults are injected as a
 * filter over MISO rather than by breaking the device, which mirrors the real
 * failures: the radio is usually fine and it is the line back from it that is
 * wrong (unconnected MISO pin, a board bus-mux left in the wrong position, or
 * another SPI device on the bus holding the line).
 */

#ifndef EVERBLU_NATIVE_CC1101_DEVICE_H
#define EVERBLU_NATIVE_CC1101_DEVICE_H

#include <Arduino.h>
#include <SPI.h>

/** Genuine CC1101 silicon revision, as reported by the VERSION register. */
inline constexpr uint8_t kCc1101Version = 0x14;
/** Genuine CC1101 PARTNUM register value. */
inline constexpr uint8_t kCc1101PartNum = 0x00;

/** How the simulated bus mangles the bytes coming back on MISO. */
enum class NativeSpiFault : uint8_t
{
    None,           ///< Healthy bus: the driver reads back what it wrote.
    StuckConstant,  ///< MISO held at a fixed value (see NativeCC1101::stuckValue).
    UnstableStatus, ///< Marginal bus: status reads alternate correct/corrupt.
};

struct NativeCC1101
{
    // ---- Fault injection -------------------------------------------------
    NativeSpiFault fault = NativeSpiFault::None;
    uint8_t stuckValue = 0xFF;
    /**
     * Whether GDO0 is actually wired to the pin the driver is watching.
     *
     * A connected GDO0 is driven LOW by the radio while it is IDLE. When false the fake
     * leaves the pin alone, so the host pull-up keeps it HIGH - exactly what the firmware
     * sees when gdo0_pin points at the wrong GPIO or at nothing at all.
     */
    bool gdo0Connected = true;

    // ---- Observability ---------------------------------------------------
    uint32_t transfers = 0;
    uint32_t statusReads = 0;
    uint32_t resetStrobes = 0;

    // ---- Simulated silicon ----------------------------------------------
    uint8_t config[0x40] = {0}; ///< Config registers 0x00-0x3F
    uint8_t partnum = kCc1101PartNum;
    uint8_t version = kCc1101Version;
    uint8_t marcstate = 0x01; ///< IDLE after reset; SRX moves it to RX

    void reset() { *this = NativeCC1101{}; }
};

inline NativeCC1101 &nativeCC1101()
{
    static NativeCC1101 device;
    return device;
}

namespace native_cc1101_detail
{

/**
 * Value a healthy device would return for one register read.
 *
 * @param address 6-bit register address taken from the header byte.
 * @param is_status True for the status-register space. The CC1101 requires the
 *        burst bit for those, so a read of PARTNUM (0xF0) arrives as a burst
 *        read of address 0x30.
 */
inline uint8_t readRegister(NativeCC1101 &device, uint8_t address, bool is_status)
{
    if (is_status)
    {
        switch (address)
        {
        case 0x30: // PARTNUM
            return device.partnum;
        case 0x31: // VERSION
            return device.version;
        case 0x35: // MARCSTATE
            return device.marcstate;
        default:
            // Status registers that are not modelled (FREQEST, LQI, RSSI,
            // TXBYTES, RXBYTES) read as zero rather than as something that
            // could be mistaken for a real measurement.
            return 0x00;
        }
    }
    if (address >= 0x3E) // PATABLE and the FIFOs are not modelled
    {
        return 0x00;
    }
    return device.config[address];
}

/** Apply the simulated bus fault to one byte on its way back to the driver. */
inline uint8_t applyFault(NativeCC1101 &device, uint8_t value, bool is_status)
{
    switch (device.fault)
    {
    case NativeSpiFault::StuckConstant:
        // A stuck line affects every byte, which is exactly what makes it so
        // hard to spot from the decoded results alone.
        return device.stuckValue;
    case NativeSpiFault::UnstableStatus:
        // A bus that mostly works but is contended or clocked too fast. Only
        // every other status read is corrupted, so any single read still looks
        // credible and only a repeated read exposes the fault.
        return (is_status && (device.statusReads % 2 == 0)) ? (uint8_t)(value ^ 0xFF) : value;
    case NativeSpiFault::None:
    default:
        return value;
    }
}

} // namespace native_cc1101_detail

/**
 * SPI transfer handler: decodes one CC1101 bus transaction in place.
 *
 * Header byte layout is the CC1101's: bit 7 read/write, bit 6 burst, bits 5-0
 * address. Command strobes are single-byte writes.
 */
inline void nativeCC1101Transfer(uint8_t *buffer, size_t length)
{
    NativeCC1101 &device = nativeCC1101();
    device.transfers++;

    // A wired GDO0 is held LOW by the radio outside a packet. The driver sets the pin to
    // INPUT_PULLUP on every init, which leaves it HIGH, so the fake has to re-assert the
    // line the same way real silicon would once it is talking on the bus.
    if (device.gdo0Connected)
    {
        digitalWrite(GDO0, LOW);
    }

    if (length == 0)
    {
        return;
    }

    const uint8_t header = buffer[0];
    const uint8_t address = (uint8_t)(header & 0x3F);
    const bool is_read = (header & 0x80) != 0;
    const bool is_burst = (header & 0x40) != 0;

    // The header byte itself clocks back the chip status byte.
    buffer[0] = native_cc1101_detail::applyFault(device, 0x00, false);

    // A single-byte transfer is a command strobe.
    if (length == 1)
    {
        if (address == 0x30) // SRES
        {
            device.resetStrobes++;
            device.marcstate = 0x01;
            memset(device.config, 0, sizeof(device.config));
        }
        else if (address == 0x34) // SRX
        {
            device.marcstate = 0x0D;
        }
        else if (address == 0x36) // SIDLE
        {
            device.marcstate = 0x01;
        }
        return;
    }

    if (is_read)
    {
        // Status registers share the address space with the command strobes and
        // are told apart by the burst bit, which the driver always sets for them
        // and never sets for a single config-register read.
        const bool is_status = is_burst && address >= 0x30;
        for (size_t i = 1; i < length; i++)
        {
            const uint8_t reg = (is_burst && !is_status) ? (uint8_t)(address + (i - 1)) : address;
            const uint8_t raw = native_cc1101_detail::readRegister(device, reg, is_status);
            buffer[i] = native_cc1101_detail::applyFault(device, raw, is_status);
            if (is_status)
            {
                device.statusReads++;
            }
        }
        return;
    }

    // Write: store what the driver sent, so a later read-back can return it.
    for (size_t i = 1; i < length; i++)
    {
        const uint8_t reg = is_burst ? (uint8_t)(address + (i - 1)) : address;
        if (reg < 0x3E)
        {
            device.config[reg] = buffer[i];
        }
        buffer[i] = native_cc1101_detail::applyFault(device, 0x00, false);
    }
}

/** Put a freshly reset simulated CC1101 on the host SPI bus. */
inline void nativeCC1101Install()
{
    nativeCC1101().reset();
    nativeSpiSetHandler(&nativeCC1101Transfer);
}

#endif // EVERBLU_NATIVE_CC1101_DEVICE_H
