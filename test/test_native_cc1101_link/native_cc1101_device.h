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

#include <vector>

#include <Arduino.h>
#include <SPI.h>

/** Genuine CC1101 silicon revision, as reported by the VERSION register. */
inline constexpr uint8_t kCc1101Version = 0x14;
/** Genuine CC1101 PARTNUM register value. */
inline constexpr uint8_t kCc1101PartNum = 0x00;

/** FIFO depth, and the threshold GDO2 is programmed to report (FIFOTHR_FIFO_THR_25_40). */
inline constexpr uint8_t kCc1101FifoSize = 64;
inline constexpr uint8_t kCc1101FifoThreshold = 25;

/**
 * Pin the simulated radio drives its GDO2 output on.
 *
 * The radio asserts the line whether or not the firmware is watching it, so the
 * fake still needs a pin in builds that opt out with DISABLE_GDO2_FIFO_MANAGEMENT.
 */
#ifdef GDO2
inline constexpr uint8_t kNativeGdo2Pin = GDO2;
#else
inline constexpr uint8_t kNativeGdo2Pin = 4;
#endif

/** Chip status states, as reported in bits 6:4 of the status byte. */
inline constexpr uint8_t kChipStateIdle = 0x00;
inline constexpr uint8_t kChipStateRx = 0x01;
inline constexpr uint8_t kChipStateTx = 0x02;
inline constexpr uint8_t kChipStateTxUnderflow = 0x07;

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
    std::vector<uint8_t> txLog; ///< Every byte the driver pushed into the TX FIFO

    // ---- Simulated silicon ----------------------------------------------
    uint8_t config[0x40] = {0}; ///< Config registers 0x00-0x3F
    uint8_t partnum = kCc1101PartNum;
    uint8_t version = kCc1101Version;
    uint8_t marcstate = 0x01;              ///< IDLE after reset; SRX moves it to RX
    uint8_t chipState = kChipStateIdle;    ///< Reported in the status byte on every transfer
    uint8_t rssiRaw = 0x0C;                ///< Raw RSSI register; 0x0C is -68 dBm, a normal link
    uint8_t lqiRaw = 0x0A;
    uint8_t freqEst = 0x00;

    // ---- Transmit --------------------------------------------------------
    uint8_t txBytes = 0;    ///< Bytes sitting in the 64-byte TX FIFO
    bool txActive = false;  ///< STX issued and the FIFO has not underflowed yet
    bool txUnderflow = false;
    /**
     * On-air rate in tenths of a byte per millisecond.
     *
     * 2.4 kbps is 300 bytes a second, so 3 tenths per ms. Draining from the
     * virtual clock rather than from bus traffic is what makes the firmware's
     * "wait for GDO2 to drop" loops behave as they do on hardware: those loops
     * only call delay(), and a FIFO that froze while they waited would look
     * exactly like the miswired GDO2 they are there to detect.
     */
    uint16_t txTenthsPerMs = 3;
    uint16_t txDrainRemainder = 0;

    // ---- Receive ---------------------------------------------------------
    /**
     * The oversampled bytes the meter answers with.
     *
     * Served through the RX FIFO in <=64-byte chunks, exactly as the driver drains
     * it. SFRX rewinds to the start, which is what lets one capture serve both the
     * ACK frame and the data frame of a single read.
     */
    std::vector<uint8_t> rxFrame;
    size_t rxPos = 0;
    /**
     * Whether SFRX rewinds the reply.
     *
     * True replays the same bytes for each receive window, which is how one
     * capture stands in for both the ACK and the data frame. False makes the
     * reply finite: once it is drained the radio hears nothing more, which is
     * what a meter that answers and then drops back to sleep looks like.
     */
    bool rxRewindOnFlush = true;
    /**
     * How many more SRX strobes should leave the radio wedged.
     *
     * A CC1101 sitting in RXFIFO_OVERFLOW ignores SRX until the FIFO is flushed.
     * Counting the strobes lets a test choose between a radio the driver's flush
     * and re-strobe recovers, and one that never comes back.
     */
    uint16_t rxEntryFailures = 0;

    size_t rxRemaining() const { return rxFrame.size() - rxPos; }

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
        case 0x32: // FREQEST
            return device.freqEst;
        case 0x33: // LQI
            return device.lqiRaw;
        case 0x34: // RSSI
            return device.rssiRaw;
        case 0x35: // MARCSTATE
            return device.marcstate;
        case 0x3A: // TXBYTES
            return (uint8_t)((device.txUnderflow ? 0x80 : 0x00) | device.txBytes);
        case 0x3B: // RXBYTES
            return (uint8_t)(device.rxRemaining() > kCc1101FifoSize ? kCc1101FifoSize
                                                                    : device.rxRemaining());
        default:
            // Status registers that are not modelled read as zero rather than as
            // something that could be mistaken for a real measurement.
            return 0x00;
        }
    }
    if (address >= 0x3E) // PATABLE is not modelled; the FIFOs are handled separately
    {
        return 0x00;
    }
    return device.config[address];
}

/** Chip status byte: bit 7 CHIP_RDYn, bits 6:4 state, bits 3:0 FIFO bytes available. */
inline uint8_t statusByte(const NativeCC1101 &device, bool is_read)
{
    // Reads report bytes waiting in the RX FIFO, writes report room in the TX FIFO.
    const size_t available = is_read ? device.rxRemaining() : (size_t)(kCc1101FifoSize - device.txBytes);
    const uint8_t nibble = (uint8_t)(available > 0x0F ? 0x0F : available);
    return (uint8_t)(((device.chipState & 0x07) << 4) | nibble);
}

/** Clock @p elapsed_ms worth of the wake-up burst out on air. */
inline void advanceTransmit(NativeCC1101 &device, unsigned long elapsed_ms)
{
    if (!device.txActive)
    {
        return;
    }
    device.txDrainRemainder = (uint16_t)(device.txDrainRemainder + elapsed_ms * device.txTenthsPerMs);
    const uint16_t sent = (uint16_t)(device.txDrainRemainder / 10);
    device.txDrainRemainder = (uint16_t)(device.txDrainRemainder % 10);

    if (sent < device.txBytes)
    {
        device.txBytes = (uint8_t)(device.txBytes - sent);
        return;
    }
    // Nothing left to send. Real silicon reports TXFIFO_UNDERFLOW, which the
    // driver treats as the normal end of the burst.
    device.txBytes = 0;
    device.txActive = false;
    device.txUnderflow = true;
    device.marcstate = 0x16;
    device.chipState = kChipStateTxUnderflow;
}

/**
 * Drive the GDO lines the way the radio would for the current FIFO state.
 *
 * GDO0 marks a sync word / packet available while receiving. GDO2 is programmed
 * as the FIFO threshold flag: above threshold in TX, data waiting in RX.
 */
inline void updateGpio(NativeCC1101 &device)
{
    if (device.gdo0Connected)
    {
        const bool packet_waiting = device.chipState == kChipStateRx && device.rxRemaining() > 0;
        digitalWrite(GDO0, packet_waiting ? HIGH : LOW);
    }
    const bool rx_threshold = device.chipState == kChipStateRx && device.rxRemaining() > 0;
    const bool tx_threshold = device.txBytes >= kCc1101FifoThreshold;
    digitalWrite(kNativeGdo2Pin, (rx_threshold || tx_threshold) ? HIGH : LOW);
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

/** Decode one bus transaction. Split out so the GDO lines can be refreshed after it. */
inline void applyTransaction(NativeCC1101 &device, uint8_t *buffer, size_t length)
{
    const uint8_t header = buffer[0];
    const uint8_t address = (uint8_t)(header & 0x3F);
    const bool is_read = (header & 0x80) != 0;
    const bool is_burst = (header & 0x40) != 0;

    // The header byte itself clocks back the chip status byte.
    buffer[0] = applyFault(device, statusByte(device, is_read), false);

    // A single-byte transfer is a command strobe.
    if (length == 1)
    {
        switch (address)
        {
        case 0x30: // SRES
            device.resetStrobes++;
            device.marcstate = 0x01;
            device.chipState = kChipStateIdle;
            device.txBytes = 0;
            device.txActive = false;
            device.txUnderflow = false;
            memset(device.config, 0, sizeof(device.config));
            break;
        case 0x34: // SRX
            if (device.rxEntryFailures > 0)
            {
                device.rxEntryFailures--;
                device.marcstate = 0x11; // RXFIFO_OVERFLOW: SRX is ignored until a flush
                break;
            }
            device.marcstate = 0x0D;
            device.chipState = kChipStateRx;
            break;
        case 0x35: // STX
            device.marcstate = 0x13;
            device.chipState = kChipStateTx;
            device.txActive = true;
            device.txUnderflow = false;
            break;
        case 0x36: // SIDLE
            device.marcstate = 0x01;
            device.chipState = kChipStateIdle;
            break;
        case 0x3A: // SFRX - rewind the reply so the next receive starts clean
            if (device.rxRewindOnFlush)
            {
                device.rxPos = 0;
            }
            break;
        case 0x3B: // SFTX
            device.txBytes = 0;
            device.txActive = false;
            device.txUnderflow = false;
            break;
        default:
            break;
        }
        return;
    }

    if (is_read)
    {
        // Status registers share the address space with the command strobes and
        // are told apart by the burst bit, which the driver always sets for them
        // and never sets for a single config-register read.
        const bool is_fifo = address == 0x3F;
        const bool is_status = is_burst && address >= 0x30 && !is_fifo;

        if (is_fifo)
        {
            for (size_t i = 1; i < length; i++)
            {
                const uint8_t raw = device.rxPos < device.rxFrame.size() ? device.rxFrame[device.rxPos++] : 0x00;
                buffer[i] = applyFault(device, raw, false);
            }
            return;
        }

        // Reading MARCSTATE is how the driver learns the burst ended; the drain
        // itself is driven by the clock, so nothing is aged here.

        for (size_t i = 1; i < length; i++)
        {
            const uint8_t reg = (is_burst && !is_status) ? (uint8_t)(address + (i - 1)) : address;
            const uint8_t raw = readRegister(device, reg, is_status);
            buffer[i] = applyFault(device, raw, is_status);
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
        if (address == 0x3F) // TX FIFO
        {
            device.txLog.push_back(buffer[i]);
            if (device.txBytes < kCc1101FifoSize)
            {
                device.txBytes++;
            }
        }
        else if (reg < 0x3E)
        {
            device.config[reg] = buffer[i];
        }
        buffer[i] = applyFault(device, statusByte(device, false), false);
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

    // The driver sets GDO0/GDO2 to INPUT_PULLUP on every init, which leaves them
    // HIGH, so the fake has to re-assert both lines the same way real silicon
    // would once it is talking on the bus.
    native_cc1101_detail::updateGpio(device);

    if (length == 0)
    {
        return;
    }

    native_cc1101_detail::applyTransaction(device, buffer, length);
    native_cc1101_detail::updateGpio(device);
}

/** Age the simulated radio as the virtual clock moves. */
inline void nativeCC1101Tick(unsigned long elapsed_ms)
{
    NativeCC1101 &device = nativeCC1101();
    native_cc1101_detail::advanceTransmit(device, elapsed_ms);
    native_cc1101_detail::updateGpio(device);
}

/** Put a freshly reset simulated CC1101 on the host SPI bus. */
inline void nativeCC1101Install()
{
    nativeCC1101().reset();
    nativeSpiSetHandler(&nativeCC1101Transfer);
    nativeClockSetTickHandler(&nativeCC1101Tick);
}

/** Arm the simulated meter with an oversampled reply captured from real hardware. */
inline void nativeCC1101ArmReply(const std::vector<uint8_t> &oversampled)
{
    nativeCC1101().rxFrame = oversampled;
    nativeCC1101().rxPos = 0;
}

#endif // EVERBLU_NATIVE_CC1101_DEVICE_H
