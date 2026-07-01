/**
 * @file fake_cc1101.cpp
 * @brief Simulated CC1101 chip + Arduino/SPI HAL backing the `native_hal` test.
 *
 * This file implements, for the host:
 *   1. The Arduino GPIO/timing primitives (digitalRead/delay/millis/...) and the
 *      SPI transport (SPIClass::transfer) declared in the native_hal shim headers.
 *   2. A behavioural model of the CC1101 radio + RADIAN meter response that is
 *      faithful enough to drive the REAL firmware in src/core/cc1101.cpp through a
 *      complete get_meter_data_for_meter() request/response cycle.
 *   3. A minimal WiFiSerial/Serial backing so utils.cpp/logging.h link.
 *
 * The chip is intercepted at the single SPI seam (wiringPiSPIDataRW ->
 * SPI.transfer) plus the GDO0/GDO2 pin reads and the delay()-driven virtual
 * clock, so no firmware logic is bypassed or reimplemented.
 */

#include "fake_cc1101.h"

#include <Arduino.h>
#include <SPI.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

// ---------------------------------------------------------------------------
// WiFiSerial / Serial backing (utils.cpp echo_debug calls WiFiSerial directly;
// logging.h remaps Serial->WiFiSerial in the firmware TUs). Define with the
// remap disabled so `Serial` here refers to the real HardwareSerial global.
// ---------------------------------------------------------------------------
#define WIFI_SERIAL_NO_REMAP
#include "wifi_serial.h"

HardwareSerial Serial;
SPIClass SPI;
WifiSerialStream WiFiSerial(Serial);

size_t WifiSerialStream::write(uint8_t c) { return _usb.write(c); }
size_t WifiSerialStream::write(const uint8_t *buffer, size_t size) { return _usb.write(buffer, size); }
size_t WifiSerialStream::printf(const char *format, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, format);
  int n = vsnprintf(buf, sizeof(buf), format, ap);
  va_end(ap);
  if (n < 0)
    return 0;
  size_t len = (static_cast<size_t>(n) < sizeof(buf)) ? static_cast<size_t>(n) : sizeof(buf) - 1;
  return _usb.write(reinterpret_cast<const uint8_t *>(buf), len);
}
void WifiSerialStream::flush() {}
int WifiSerialStream::available() { return 0; }
int WifiSerialStream::read() { return -1; }
int WifiSerialStream::peek() { return -1; }
void WifiSerialStream::beginServer() {}
void WifiSerialStream::loop() {}

// ---------------------------------------------------------------------------
// CC1101 register / strobe constants (mirrors src/core/cc1101.cpp addressing).
// ---------------------------------------------------------------------------
namespace
{
// Config registers (indexed by addr & 0x3F for addr < 0x30)
constexpr uint8_t REG_IOCFG2 = 0x00;
constexpr uint8_t REG_IOCFG0 = 0x02;
constexpr uint8_t REG_SYNC0 = 0x05;
constexpr uint8_t REG_PKTLEN = 0x06;
constexpr uint8_t REG_PKTCTRL0 = 0x08;

// Status registers (addr & 0x3F for addr >= 0x30 on a read)
constexpr uint8_t ST_PARTNUM = 0x30;
constexpr uint8_t ST_VERSION = 0x31;
constexpr uint8_t ST_FREQEST = 0x32;
constexpr uint8_t ST_LQI = 0x33;
constexpr uint8_t ST_RSSI = 0x34;
constexpr uint8_t ST_MARCSTATE = 0x35;
constexpr uint8_t ST_TXBYTES = 0x3A;
constexpr uint8_t ST_RXBYTES = 0x3B;

// FIFO addresses (addr & 0x3F == 0x3F); PATABLE == 0x3E
constexpr uint8_t ADDR_FIFO = 0x3F;
constexpr uint8_t ADDR_PATABLE = 0x3E;

// Command strobes
constexpr uint8_t SRES = 0x30;
constexpr uint8_t SCAL = 0x33;
constexpr uint8_t SRX = 0x34;
constexpr uint8_t STX = 0x35;
constexpr uint8_t SIDLE = 0x36;
constexpr uint8_t SFRX = 0x3A;
constexpr uint8_t SFTX = 0x3B;

// Chip STATE field (top nibble of the status byte)
constexpr uint8_t STATE_IDLE = 0x0;
constexpr uint8_t STATE_RX = 0x1;
constexpr uint8_t STATE_TX = 0x2;
constexpr uint8_t STATE_TXUNF = 0x7;

// MARCSTATE values referenced by the firmware
constexpr uint8_t MARC_IDLE = 0x01;
constexpr uint8_t MARC_RX = 0x0D;
constexpr uint8_t MARC_TX = 0x13;
constexpr uint8_t MARC_TXUNF = 0x16;

// Register values used to detect the RX phases (see cc1101.cpp).
constexpr uint8_t SYNC0_STAGE1 = 0x50; // SYNC0_PATTERN_50 (with PKTLEN==1)
constexpr uint8_t SYNC0_STAGE2 = 0xF0; // SYNC0_PATTERN_F0 (with PKTCTRL0 infinite)
constexpr uint8_t PKTCTRL0_INFINITE = 0x02;

constexpr uint8_t IOCFG2_TX_THR = 0x02;     // GDO2 HIGH when TX FIFO >= 25 bytes
constexpr uint8_t IOCFG2_RX_THR_EOP = 0x01; // GDO2 HIGH when RX FIFO >= 40 bytes

constexpr int TX_FIFO_THRESHOLD = 25;
constexpr int RX_FIFO_THRESHOLD = 40;
constexpr int TX_FIFO_CAP = 64;

// TX FIFO drain rate (bytes per virtual millisecond). Slow enough that the
// firmware's feed loop keeps the FIFO non-empty during the wake-up burst, but
// the FIFO empties (-> underflow) once feeding stops after the interrogation
// frame, reproducing the MARCSTATE 0x16 end-of-transmit the firmware waits for.
constexpr double TX_DRAIN_BYTES_PER_MS = 0.8;
} // namespace

// ---------------------------------------------------------------------------
// Simulated chip
// ---------------------------------------------------------------------------
struct FakeCC1101
{
  uint8_t regs[0x40] = {0};

  uint8_t state = STATE_IDLE;
  uint8_t marcstate = MARC_IDLE;

  // TX FIFO
  int tx_count = 0;
  bool tx_underflow = false;
  double tx_drain_accum = 0.0;
  int tx_empty_streak = 0;

  // RX FIFO (bytes queued for the current receive)
  std::vector<uint8_t> rx_data;
  size_t rx_pos = 0;
  bool gdo0_high = false;

  // Stage-2 payloads to hand back, in order (ACK then DATA).
  std::deque<std::vector<uint8_t>> stage2_payloads;

  // Virtual clock
  uint64_t now_us = 0;

  void reset_chip()
  {
    state = STATE_IDLE;
    marcstate = MARC_IDLE;
    tx_count = 0;
    tx_underflow = false;
    tx_drain_accum = 0.0;
    tx_empty_streak = 0;
    rx_data.clear();
    rx_pos = 0;
    gdo0_high = false;
  }

  size_t rx_remaining() const { return rx_data.size() - rx_pos; }

  uint8_t status_byte() const
  {
    uint8_t nibble = 0x0F;
    if (state == STATE_RX)
      nibble = static_cast<uint8_t>(rx_remaining() > 15 ? 15 : rx_remaining());
    else if (state == STATE_TX)
    {
      int freeb = TX_FIFO_CAP - tx_count;
      nibble = static_cast<uint8_t>(freeb > 15 ? 15 : (freeb < 0 ? 0 : freeb));
    }
    return static_cast<uint8_t>((state << 4) | (nibble & 0x0F));
  }

  // Advance the virtual clock and drain the TX FIFO while transmitting.
  void advance(double ms)
  {
    now_us += static_cast<uint64_t>(ms * 1000.0);
    if (state == STATE_TX && !tx_underflow)
    {
      tx_drain_accum += ms * TX_DRAIN_BYTES_PER_MS;
      int d = static_cast<int>(tx_drain_accum);
      if (d > 0)
      {
        tx_drain_accum -= d;
        tx_count -= d;
        if (tx_count <= 0)
        {
          tx_count = 0;
          if (++tx_empty_streak >= 2)
          {
            tx_underflow = true;
            state = STATE_TXUNF;
            marcstate = MARC_TXUNF;
          }
        }
        else
        {
          tx_empty_streak = 0;
        }
      }
    }
  }

  void begin_receive()
  {
    const uint8_t sync0 = regs[REG_SYNC0];
    const uint8_t pktlen = regs[REG_PKTLEN];
    const uint8_t pktctrl0 = regs[REG_PKTCTRL0];

    state = STATE_RX;
    marcstate = MARC_RX;

    if (sync0 == SYNC0_STAGE1 && pktlen == 1)
    {
      // Stage 1: initial sync detection. Hand back a short sync burst.
      gdo0_high = true;
      rx_data.assign(8, 0x55);
      rx_pos = 0;
    }
    else if (sync0 == SYNC0_STAGE2 && pktctrl0 == PKTCTRL0_INFINITE)
    {
      // Stage 2: deliver the next queued frame (ACK, then DATA).
      gdo0_high = true;
      if (!stage2_payloads.empty())
      {
        rx_data = stage2_payloads.front();
        stage2_payloads.pop_front();
      }
      else
      {
        rx_data.clear();
      }
      rx_pos = 0;
    }
    else
    {
      // Plain RX (e.g. cc1101_rec_mode after init); no frame, no GDO0 event.
      gdo0_high = false;
    }
  }

  void strobe(uint8_t cmd)
  {
    switch (cmd)
    {
    case SRES:
      reset_chip();
      break;
    case SCAL:
      state = STATE_IDLE;
      marcstate = MARC_IDLE;
      break;
    case SRX:
      begin_receive();
      break;
    case STX:
      state = STATE_TX;
      marcstate = MARC_TX;
      tx_underflow = false;
      tx_empty_streak = 0;
      tx_drain_accum = 0.0;
      break;
    case SIDLE:
      if (state != STATE_TXUNF)
      {
        state = STATE_IDLE;
        marcstate = MARC_IDLE;
      }
      else
      {
        state = STATE_IDLE;
        marcstate = MARC_IDLE;
      }
      break;
    case SFRX:
      rx_data.clear();
      rx_pos = 0;
      break;
    case SFTX:
      tx_count = 0;
      tx_underflow = false;
      tx_empty_streak = 0;
      tx_drain_accum = 0.0;
      if (state == STATE_TXUNF)
      {
        state = STATE_IDLE;
        marcstate = MARC_IDLE;
      }
      break;
    default:
      break;
    }
  }

  uint8_t read_status_reg(uint8_t idx)
  {
    switch (idx)
    {
    case ST_PARTNUM:
      return 0x00;
    case ST_VERSION:
      return 0x14; // must be != 0x00 and != 0xFF
    case ST_FREQEST:
      return 0x00;
    case ST_LQI:
      return 0x50;
    case ST_RSSI:
      return 254; // -> -75 dBm via cc1100_rssi_convert2dbm
    case ST_MARCSTATE:
      return marcstate;
    case ST_TXBYTES:
      return static_cast<uint8_t>((tx_underflow ? 0x80 : 0x00) | (tx_count & 0x7F));
    case ST_RXBYTES:
    {
      size_t r = rx_remaining();
      if (r > 64)
        r = 64;
      return static_cast<uint8_t>(r & 0x7F);
    }
    default:
      return 0x00;
    }
  }

  // Full-duplex SPI exchange: fill buf with MISO bytes, apply MOSI side effects.
  void spi_transfer(uint8_t *buf, size_t len)
  {
    if (len == 0)
      return;

    const uint8_t hdr = buf[0];
    const bool is_read = (hdr & 0x80) != 0;
    const bool is_burst = (hdr & 0x40) != 0;
    const uint8_t addr = hdr & 0x3F;
    const uint8_t sts = status_byte();

    if (len == 1)
    {
      // Command strobe.
      strobe(addr);
      buf[0] = status_byte();
      return;
    }

    if (!is_burst)
    {
      if (is_read)
      {
        buf[0] = sts;
        buf[1] = (addr >= 0x30) ? read_status_reg(addr) : regs[addr];
      }
      else
      {
        regs[addr] = buf[1];
        buf[0] = status_byte();
        buf[1] = status_byte();
      }
      return;
    }

    // Burst transfer: buf[0] is the header, buf[1..len-1] is the payload.
    const size_t n = len - 1;
    if (is_read)
    {
      buf[0] = sts;
      if (addr == ADDR_FIFO)
      {
        for (size_t i = 0; i < n; ++i)
        {
          buf[1 + i] = (rx_pos < rx_data.size()) ? rx_data[rx_pos++] : 0x00;
        }
      }
      else if (addr >= 0x30)
      {
        // Status registers carry the burst bit but are single-byte reads
        // (halRfReadReg on MARCSTATE/TXBYTES/RXBYTES/VERSION/...).
        buf[1] = read_status_reg(addr);
      }
      else
      {
        for (size_t i = 0; i < n; ++i)
          buf[1 + i] = regs[(addr + i) & 0x3F];
      }
    }
    else
    {
      if (addr == ADDR_FIFO)
      {
        tx_count += static_cast<int>(n);
        if (tx_count > 0)
          tx_empty_streak = 0;
      }
      else if (addr == ADDR_PATABLE)
      {
        // PA table write: ignore contents.
      }
      else
      {
        for (size_t i = 0; i < n; ++i)
          regs[(addr + i) & 0x3F] = buf[1 + i];
      }
      // Every returned byte reflects the status byte (firmware reads buf[len]).
      for (size_t i = 0; i < len; ++i)
        buf[i] = status_byte();
    }
  }

  int digital_read(uint8_t pin)
  {
    if (pin == static_cast<uint8_t>(GDO0))
      return gdo0_high ? HIGH : LOW;
    if (pin == static_cast<uint8_t>(GDO2))
    {
      const uint8_t iocfg2 = regs[REG_IOCFG2];
      if (iocfg2 == IOCFG2_TX_THR)
        return (tx_count >= TX_FIFO_THRESHOLD) ? HIGH : LOW;
      if (iocfg2 == IOCFG2_RX_THR_EOP)
        return (static_cast<int>(rx_remaining()) >= RX_FIFO_THRESHOLD) ? HIGH : LOW;
      return LOW;
    }
    return LOW;
  }
};

FakeCC1101 g_fake_chip;

void fake_cc1101_reset()
{
  g_fake_chip = FakeCC1101();
}

void fake_cc1101_queue_stage2(const uint8_t *data, size_t len)
{
  g_fake_chip.stage2_payloads.emplace_back(data, data + len);
}

// ---------------------------------------------------------------------------
// Arduino HAL primitives
// ---------------------------------------------------------------------------
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin) { return g_fake_chip.digital_read(pin); }
void delay(unsigned long ms) { g_fake_chip.advance(static_cast<double>(ms)); }
void delayMicroseconds(unsigned int us) { g_fake_chip.advance(static_cast<double>(us) / 1000.0); }
unsigned long millis() { return static_cast<unsigned long>(g_fake_chip.now_us / 1000ULL); }
unsigned long micros() { return static_cast<unsigned long>(g_fake_chip.now_us); }
void yield() {}

// ---------------------------------------------------------------------------
// SPI transport
// ---------------------------------------------------------------------------
uint8_t SPIClass::transfer(uint8_t data)
{
  uint8_t b = data;
  g_fake_chip.spi_transfer(&b, 1);
  return b;
}
void SPIClass::transfer(void *buf, size_t count)
{
  g_fake_chip.spi_transfer(static_cast<uint8_t *>(buf), count);
}
