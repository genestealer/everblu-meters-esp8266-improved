/*  CC1101 radio interface for Itron EverBlu Cyble Enhanced water meters */
/*  Implements RADIAN protocol communication over 433 MHz RF */

#include "utils.h"  // Utility functions
#include "cc1101.h" // CC1101 interface
#include "meter_code_parser.h"
#include "radian_parser.h"
#include "radian_decoder.h" // Shared platform-neutral 4-bit-per-bit decoder
#include "logging.h" // Cross-platform logging
#include <Arduino.h> // Arduino core
#if !defined(USE_ESPHOME)
#if defined(__has_include)
#if __has_include("private.h")
#include "private.h" // Standalone firmware configuration (optional)
#endif
#else
/* No __has_include support; skip optional private.h */
#endif
#endif
#include "wifi_serial.h" // Optional WiFi serial mirroring
#if !defined(USE_ESPHOME)
#include <SPI.h> // Include the SPI library for SPI communication (not needed for ESPHome)
#endif
#ifdef USE_ESPHOME
#include "esphome/components/spi/spi.h" // ESPHome SPI device interface
#endif
#if defined(ESP32)
#include <esp_task_wdt.h>
#endif

// Cross-platform watchdog feed helper
static inline void FEED_WDT()
{
#if defined(ESP8266)
  ESP.wdtFeed();
  yield(); // Yield to keep the SDK/WiFi task serviced during long radio operations
#elif defined(ESP32)
  esp_task_wdt_reset();
  yield();
#else
  (void)0;
#endif
}

uint8_t RF_config_u8 = 0xFF;
uint8_t PA[] = {
    0x60,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};
uint8_t CC1101_status_state = 0;
uint8_t CC1101_status_FIFO_FreeByte = 0;
uint8_t CC1101_status_FIFO_ReadByte = 0;
// Enable detailed CC1101 / RADIAN debug output when set to 1.
// This value may be configured in your `include/private.h` by setting
// `#define DEBUG_CC1101 0` (disable) or `#define DEBUG_CC1101 1` (enable).
// If not defined in private.h, fall back to enabled (1) to preserve
// the previous default behaviour.
#ifndef DEBUG_CC1101
#define DEBUG_CC1101 0
#endif
static const uint8_t debug_out = (uint8_t)(DEBUG_CC1101);

#ifndef TRUE
#define TRUE true
#endif

#ifndef FALSE
#define FALSE false
#endif

#define TX_LOOP_OUT 300
/*---------------------------[CC1100 - R/W offsets]------------------------------*/
#define WRITE_SINGLE_BYTE 0x00
#define WRITE_BURST 0x40
#define READ_SINGLE_BYTE 0x80
#define READ_BURST 0xC0

/*-------------------------[CC1100 - config register]----------------------------*/
#define IOCFG2 0x00 // GDO2 output pin configuration
#define IOCFG1 0x01 // GDO1 output pin configuration
#define IOCFG0 0x02 // GDO0 output pin configuration

/*-------------------------[CC1100 - Register Values]----------------------------*/
// IOCFG2 - GDO2 Output Pin Configuration
// GDO2's meaning is switched dynamically per phase: TX feeding uses the TX FIFO
// threshold signal (0x02); RADIAN frame reception uses the RX FIFO threshold /
// end-of-packet signal (0x01) so the RX drain loop can skip needless RXBYTES reads.
#define IOCFG2_SERIAL_DATA_OUTPUT 0x0D // GDO2: Serial Data Output (kept for reference, not used)
#define IOCFG2_TX_FIFO_THR 0x02        // GDO2: asserts HIGH when TX FIFO >= threshold; de-asserts LOW when below threshold
#define IOCFG2_RX_FIFO_THR_OR_EOP 0x01 // GDO2: asserts HIGH when RX FIFO >= threshold OR end-of-packet; de-asserts LOW when RX FIFO empty

// IOCFG0 - GDO0 Output Pin Configuration
#define IOCFG0_SYNC_WORD_DETECT 0x06 // Asserts when sync word detected, deasserts at end of packet

// FIFOTHR - RX FIFO and TX FIFO Thresholds
// FIFO_THR=9 (0x49): TX threshold=25 bytes - de-assertion guarantees >=40 free bytes, safely fits both
//                    the 8-byte WUP buffer and the 39-byte interrogation frame with a single GDO2 check.
//                    RX threshold=40 bytes - GDO2 (IOCFG2=0x01) asserts once the RX FIFO holds >=40 bytes,
//                    letting the RX drain loop skip RXBYTES SPI reads until a worthwhile chunk is buffered.
#define FIFOTHR_FIFO_THR_33_32 0x47 // FIFO_THR=7: TX threshold 33 bytes, RX threshold 33 bytes (legacy)
#define FIFOTHR_FIFO_THR_25_40 0x49 // FIFO_THR=9: TX threshold 25 bytes, RX threshold 40 bytes

// RX FIFO threshold in bytes for FIFOTHR_FIFO_THR_25_40 (FIFO_THR=9). The RX drain
// loop uses this to know when the GDO2 fast-path no longer applies: the final tail
// of a frame (< this many bytes) never raises GDO2 under infinite packet length
// (no end-of-packet), so RXBYTES polling must resume to drain it.
#define RX_FIFO_THRESHOLD_BYTES 40

// SYNC1/SYNC0 - Sync Word Configuration
#define SYNC1_PATTERN_55 0x55 // Sync word pattern: 0x55 (01010101)
#define SYNC0_PATTERN_00 0x00 // Sync word pattern: 0x00 (00000000)
#define SYNC0_PATTERN_50 0x50 // Sync word pattern: 0x50 (01010000)
#define SYNC1_PATTERN_FF 0xFF // Sync word pattern: 0xFF (11111111)
#define SYNC0_PATTERN_F0 0xF0 // Sync word pattern: 0xF0 (11110000)

// PKTCTRL1 - Packet Automation Control
#define PKTCTRL1_NO_ADDR_CHECK 0x00 // No address check, no status appended

// PKTCTRL0 - Packet Automation Control
#define PKTCTRL0_FIXED_LENGTH 0x00    // Fixed packet length mode
#define PKTCTRL0_INFINITE_LENGTH 0x02 // Infinite packet length mode

// FSCTRL1 - Frequency Synthesizer Control
#define FSCTRL1_FREQ_IF 0x08 // Intermediate frequency

// MDMCFG4 - Modem Configuration
// RX filter bandwidth = Fxosc / (8 * (4 + CHANBW_M) * 2^CHANBW_E).
// 0x66: CHANBW_E=1, CHANBW_M=2, DRATE_E=6 -> BW = 26000/(8*6*2) = 270.8 kHz.
// The wide 270 kHz filter lets the CC1101's own frequency-offset compensation
// (see FOCCFG, +-BW/4 = +-67.7 kHz) absorb even a badly out-of-spec reference
// crystal (~150 ppm) so the radio locks at the nominal 433.82 MHz carrier
// without any software frequency scanning. The RADIAN signal itself is only
// ~15 kHz wide (2.4 kbps, 5.157 kHz deviation), so the extra bandwidth costs
// ~6.7 dB of noise floor - negligible against the typical >20 dB link margin.
#define MDMCFG4_RX_BW_270KHZ 0x66        // RX filter bandwidth = 270 kHz, 2.4 kbps
#define MDMCFG4_RX_BW_58KHZ 0xF6         // RX filter bandwidth = 58 kHz, 2.4 kbps (legacy narrow)
#define MDMCFG4_RX_BW_58KHZ_9_6KBPS 0xF8 // RX filter bandwidth = 58 kHz, 9.6 kbps (4x oversampling)

// MDMCFG3 - Modem Configuration (Data Rate)
#define MDMCFG3_DRATE_2_4KBPS 0x83 // Data rate: 2.4 kbps (26M*((256+0x83)*2^6)/2^28)

// MDMCFG2 - Modem Configuration (Modulation, Sync)
#define MDMCFG2_2FSK_16_16_SYNC 0x02  // 2-FSK, no Manchester, 16/16 sync word bits
#define MDMCFG2_NO_PREAMBLE_SYNC 0x00 // No preamble/sync transmission

// MDMCFG1 - Modem Configuration (Preamble, Channel Spacing)
#define MDMCFG1_NUM_PREAMBLE_2 0x00 // 2 preamble bytes, channel spacing exponent

// MDMCFG0 - Modem Configuration (Channel Spacing)
#define MDMCFG0_CHANSPC_25KHZ 0x00 // Channel spacing = 25 kHz

// DEVIATN - Modem Deviation Setting
#define DEVIATN_5_157KHZ 0x15 // Deviation = 5.157 kHz

// MCSM1 - Main Radio Control State Machine
#define MCSM1_CCA_ALWAYS_IDLE 0x00 // CCA always, idle on exit
#define MCSM1_CCA_ALWAYS_RX 0x0F   // CCA always, RX on exit

// MCSM0 - Main Radio Control State Machine
#define MCSM0_FS_AUTOCAL_IDLE_TO_RXTX 0x18 // Auto-calibrate from IDLE to RX/TX

// FOCCFG - Frequency Offset Compensation
// 0x1E: FOC enabled, 4K before sync, K/2 after sync, FOC_LIMIT = +-BW/4.
// With the 270 kHz RX bandwidth this gives +-67.7 kHz of automatic carrier
// offset correction (~+-156 ppm at 433 MHz), enough to lock onto the meter at
// the nominal frequency even with a significantly off-spec reference crystal.
#define FOCCFG_FOC_4K_2K 0x1E // FOC enabled, 4K before sync, K/2 after sync, FOC_LIMIT = +-BW/4

// BSCFG - Bit Synchronization Configuration
#define BSCFG_BS_PRE_KI_2 0x1C // Bit sync configuration

// AGCCTRL2 - AGC Control (balanced profile)
// 0x43 = 01 000 011: MAX_DVGA_GAIN=01 (more DVGA headroom), MAX_LNA_GAIN=000, MAGN_TARGET=011 (33 dB)
// Replaces the former 0xC7 (42 dB target, 3 DVGA steps disabled) which locked the receiver near
// maximum gain and caused front-end saturation / CRC failures at close range (< 0.5 m).
// The 9 dB lower target gives the AGC loop headroom to reduce gain for strong near-field signals
// without degrading sensitivity for weak/distant meters.
#define AGCCTRL2_BALANCED 0x43

// AGCCTRL2 values with optional LNA gain reduction (RX_ATTENUATION_DB).
// Each step limits MAX_LNA_GAIN in AGCCTRL2[5:3] while preserving the 33 dB MAGN_TARGET.
// Approximate actual reduction per CC1101 datasheet MAX_LNA_GAIN table:
//   0 dB  -> MAX_LNA_GAIN=000 (no limit)          -> 0x43
//   6 dB  -> MAX_LNA_GAIN=010 (6.1 dB reduction)  -> 0x53
//  12 dB  -> MAX_LNA_GAIN=101 (11.5 dB reduction) -> 0x6B
//  18 dB  -> MAX_LNA_GAIN=111 (17.1 dB reduction) -> 0x7B
#define AGCCTRL2_ATT_0DB  0x43
#define AGCCTRL2_ATT_6DB  0x53
#define AGCCTRL2_ATT_12DB 0x6B
#define AGCCTRL2_ATT_18DB 0x7B

// AGCCTRL1 - AGC Control
#define AGCCTRL1_DEFAULT 0x00 // Default AGC control

// AGCCTRL0 - AGC Control
#define AGCCTRL0_FILTER_16 0xB2 // AGC filter length = 16 samples

// WORCTRL - Wake On Radio Control
#define WORCTRL_WOR_RES_1_8 0xFB // WOR resolution 1/8 seconds

// FREND1 - Front End RX Configuration
#define FREND1_LNA_CURRENT 0xB6 // LNA current setting

// TEST2/TEST1/TEST0 - Test Settings
#define TEST2_RX_LOW_DATA_RATE 0x81 // Various test settings (RX low data rate)
#define TEST1_RX_LOW_DATA_RATE 0x35 // Various test settings (RX low data rate)
#define TEST0_RX_LOW_DATA_RATE 0x09 // Various test settings (RX low data rate)
#define FIFOTHR 0x03                // RX FIFO and TX FIFO thresholds
#define SYNC1 0x04                  // Sync word, high byte
#define SYNC0 0x05                  // Sync word, low byte
#define PKTLEN 0x06                 // Packet length
#define PKTCTRL1 0x07               // Packet automation control
#define PKTCTRL0 0x08               // Packet automation control
#define ADDRR 0x09                  // Device address
#define CHANNR 0x0A                 // Channel number
#define FSCTRL1 0x0B                // Frequency synthesizer control
#define FSCTRL0 0x0C                // Frequency synthesizer control
#define FREQ2 0x0D                  // Frequency control word, high byte
#define FREQ1 0x0E                  // Frequency control word, middle byte
#define FREQ0 0x0F                  // Frequency control word, low byte

#define MDMCFG4 0x10  // Modem configuration
#define MDMCFG3 0x11  // Modem configuration
#define MDMCFG2 0x12  // Modem configuration
#define MDMCFG1 0x13  // Modem configuration
#define MDMCFG0 0x14  // Modem configuration
#define DEVIATN 0x15  // Modem deviation setting
#define MCSM2 0x16    // Main Radio Cntrl State Machine config
#define MCSM1 0x17    // Main Radio Cntrl State Machine config
#define MCSM0 0x18    // Main Radio Cntrl State Machine config
#define FOCCFG 0x19   // Frequency Offset Compensation config
#define BSCFG 0x1A    // Bit Synchronization configuration
#define AGCCTRL2 0x1B // AGC control
#define AGCCTRL1 0x1C // AGC control
#define AGCCTRL0 0x1D // AGC control
#define WOREVT1 0x1E  // High byte Event 0 timeout
#define WOREVT0 0x1F  // Low byte Event 0 timeout

#define WORCTRL 0x20 // Wake On Radio control
#define FREND1 0x21  // Front end RX configuration
#define FREND0 0x22  // Front end TX configuration
#define FSCAL3 0x23  // Frequency synthesizer calibration
#define FSCAL2 0x24  // Frequency synthesizer calibration
#define FSCAL1 0x25  // Frequency synthesizer calibration
#define FSCAL0 0x26  // Frequency synthesizer calibration
#define RCCTRL1 0x27 // RC oscillator configuration
#define RCCTRL0 0x28 // RC oscillator configuration
#define FSTEST 0x29  // Frequency synthesizer cal control
#define PTEST 0x2A   // Production test
#define AGCTEST 0x2B // AGC test
#define TEST2 0x2C   // Various test settings
#define TEST1 0x2D   // Various test settings
#define TEST0 0x2E   // Various test settings
#ifdef USE_ESPHOME
// ESPHome SPI integration - store the SPIDevice instance used for transactions.
// The EverbluMeterComponent inherits from this SPIDevice specialization.
using CC1101SpiDevice = esphome::spi::SPIDevice<esphome::spi::BIT_ORDER_MSB_FIRST,
                                                esphome::spi::CLOCK_POLARITY_LOW,
                                                esphome::spi::CLOCK_PHASE_LEADING,
                                                esphome::spi::DATA_RATE_1MHZ>;
static CC1101SpiDevice *_spi_device = nullptr;
static int _gdo0_pin = -1;
static int _gdo2_pin = -1;
static int _rx_attenuation_db = 0;

void cc1101_set_spi_device(void *device)
{
  // CS is managed by ESPHome SPIDevice enable()/disable().
  _spi_device = static_cast<CC1101SpiDevice *>(device);
}

void cc1101_set_gdo0_pin(int gdo0_pin)
{
  _gdo0_pin = gdo0_pin;
}

void cc1101_set_gdo2_pin(int gdo2_pin)
{
  _gdo2_pin = gdo2_pin;
}

void cc1101_set_rx_attenuation(int db)
{
  _rx_attenuation_db = db;
}

// Macros to get GDO pin numbers - use variables in ESPHome mode, build flags otherwise
#define GET_GDO0_PIN() (_gdo0_pin)
#define GET_GDO2_PIN() (_gdo2_pin)
#else
// Non-ESPHome mode: GDO0 comes from build flag.
// GDO2 hardware-assisted FIFO management is ENABLED BY DEFAULT (v3.0.0+, breaking change).
// Wire CC1101 GDO2 to a free GPIO and add '#define GDO2 <pin>' to include/private.h.
// To keep the legacy SPI-polling behaviour instead, add '#define DISABLE_GDO2_FIFO_MANAGEMENT'.
#define GET_GDO0_PIN() (GDO0)
#if defined(GDO2)
#define GET_GDO2_PIN() (GDO2)
#elif defined(DISABLE_GDO2_FIFO_MANAGEMENT)
#define GET_GDO2_PIN() (-1)
#else
#error "BREAKING CHANGE (v3.0.0): CC1101 GDO2 hardware-assisted FIFO management is now enabled by default. Wire CC1101 GDO2 to a free GPIO and add '#define GDO2 <pin>' to include/private.h (see the README Hardware section and docs/GDO2_FIFO_MANAGEMENT.md). To keep the legacy SPI-polling behaviour instead, add '#define DISABLE_GDO2_FIFO_MANAGEMENT' to include/private.h."
#endif
#endif

// Diagnostic counter shared by both build targets: number of times the TX
// interrogation-frame gate waited the full safety limit with GDO2 still HIGH (the FIFO
// never reported below threshold). A non-zero, growing value almost always means GDO2
// is miswired / on the wrong GPIO / not connected, rather than an RF/meter problem.
// Monotonic (lifetime) counter; surfaced via cc1101_get_gdo2_timeout_count() for telemetry.
static uint32_t _gdo2_stuck_timeouts = 0;

uint32_t cc1101_get_gdo2_timeout_count(void)
{
  return _gdo2_stuck_timeouts;
}

// Change these define according to your ESP8266 board
#if defined(ESP8266) && !defined(USE_ESPHOME)
#define SPI_CSK PIN_SPI_SCK
#define SPI_MISO PIN_SPI_MISO
#define SPI_MOSI PIN_SPI_MOSI
#define SPI_SS PIN_SPI_SS
#endif

// Change these define according to your ESP32 board
#if defined(ESP32) && !defined(USE_ESPHOME)
#define SPI_CSK SCK
#define SPI_MISO MISO
#define SPI_MOSI MOSI
#define SPI_SS SS
#endif

int _spi_speed = 0;
int wiringPiSPIDataRW(int channel, unsigned char *data, int len)
{
#ifdef USE_ESPHOME
  // ESPHome mode: Use SPIDevice methods (enable/transfer_array/disable)
  // The SPIDevice handles bus configuration, speed, and transaction management
  if (!_spi_device)
    return -1;

  _spi_device->enable();
  _spi_device->transfer_array(data, len);
  _spi_device->disable();
#else
  // Arduino SPI
  if (!_spi_speed)
    return -1;

  SPI.beginTransaction(SPISettings(_spi_speed, MSBFIRST, SPI_MODE0));
  digitalWrite(SPI_SS, LOW);
  SPI.transfer(data, len);
  digitalWrite(SPI_SS, HIGH);
  SPI.endTransaction();
#endif

  return 0;
}

int wiringPiSPISetup(int channel, int speed)
{
#ifdef USE_ESPHOME
  // ESPHome mode: SPI is already configured by SPIDevice.
  // The requested speed is retained only for diagnostics; transfers use the
  // rate defined by the SPIDevice template in the ESPHome component.
  _spi_speed = speed;
  if (!_spi_device)
    return -1;
#else
  // Arduino mode: Set up SPI manually
  _spi_speed = speed;

  pinMode(SPI_SS, OUTPUT);
  digitalWrite(SPI_SS, HIGH);

#ifdef ESP8266
  SPI.pins(SPI_CSK, SPI_MISO, SPI_MOSI, SPI_SS);
  SPI.begin();
#endif

#ifdef ESP32
  SPI.begin(SPI_CSK, SPI_MISO, SPI_MOSI, SPI_SS);
#endif
#endif

  return 0;
}

/*----------------------------[END config register]------------------------------*/
//------------------[write register]--------------------------------
uint8_t halRfWriteReg(uint8_t reg_addr, uint8_t value)
{
  uint8_t tbuf[2] = {0};
  tbuf[0] = reg_addr | WRITE_SINGLE_BYTE;
  tbuf[1] = value;
  uint8_t len = 2;
  wiringPiSPIDataRW(0, tbuf, len);
  CC1101_status_FIFO_FreeByte = tbuf[1] & 0x0F;
  CC1101_status_state = (tbuf[0] >> 4) & 0x0F;

  return TRUE;
}

/*-------------------------[CC1100 - status register]----------------------------*/
/* 0x3? is replace by 0xF? because for status register burst bit shall be set */
#define PARTNUM_ADDR 0xF0    // Part number
#define VERSION_ADDR 0xF1    // Current version number
#define FREQEST_ADDR 0xF2    // Frequency offset estimate
#define LQI_ADDR 0xF3        // Demodulator estimate for link quality
#define RSSI_ADDR 0xF4       // Received signal strength indication
#define MARCSTATE_ADDR 0xF5  // Control state machine state
#define WORTIME1_ADDR 0xF6   // High byte of WOR timer
#define WORTIME0_ADDR 0xF7   // Low byte of WOR timer
#define PKTSTATUS_ADDR 0xF8  // Current GDOx status and packet status
#define VCO_VC_DAC_ADDR 0xF9 // Current setting from PLL cal module
#define TXBYTES_ADDR 0xFA    // Underflow and # of bytes in TXFIFO
#define RXBYTES_ADDR 0xFB    // Overflow and # of bytes in RXFIFO
//----------------------------[END status register]-------------------------------
#define RXBYTES_MASK 0x7F // Mask "# of bytes" field in _RXBYTES

uint8_t halRfReadReg(uint8_t spi_instr)
{
  uint8_t value;
  uint8_t rbuf[2] = {0};
  uint8_t len = 2;

  // CC1101 Errata Note: Read register value to update status bytes
  rbuf[0] = spi_instr | READ_SINGLE_BYTE;
  rbuf[1] = 0;
  wiringPiSPIDataRW(0, rbuf, len);
  CC1101_status_FIFO_ReadByte = rbuf[0] & 0x0F;
  CC1101_status_state = (rbuf[0] >> 4) & 0x0F;
  value = rbuf[1];
  return value;
}

#define PATABLE_ADDR 0x3E // Pa Table Adress
#define TX_FIFO_ADDR 0x3F
#define RX_FIFO_ADDR 0xBF

// Maximum SPI burst transfer size (should be >= largest expected transfer)
// RADIAN protocol can receive ~682 bytes (4x oversampled 170-byte frame)
#define MAX_SPI_BURST_SIZE 1024

void SPIReadBurstReg(uint8_t spi_instr, uint8_t *pArr, uint8_t len)
{
  // Use static buffer to avoid stack overflow on ESP8266 (stack ~4KB only)
  // VLA (variable-length array) on stack was causing silent memory corruption
  // NOTE: Static buffer is safe in this single-threaded application where SPI
  // operations are serialized and never called from interrupt context.
  // SPI transactions are protected by beginTransaction() which disables interrupts.
  static uint8_t rbuf[MAX_SPI_BURST_SIZE + 1];
  uint8_t i = 0;

  // Bounds check to prevent buffer overflow
  if (len > MAX_SPI_BURST_SIZE)
  {
    echo_debug(1, "[ERROR] SPI burst read too large (%u > %u)\n", (unsigned)len, (unsigned)MAX_SPI_BURST_SIZE);
    return;
  }

  // Feed watchdog before long operations to prevent timeout
  if (len > 64)
  {
    FEED_WDT();
  }

  memset(rbuf, 0, len + 1);
  rbuf[0] = spi_instr | READ_BURST;
  wiringPiSPIDataRW(0, rbuf, len + 1);
  for (i = 0; i < len; i++)
  {
    pArr[i] = rbuf[i + 1];
    // echo_debug(debug_out,"SPI_arr_read: 0x%02X\n", pArr[i]);
  }
  CC1101_status_FIFO_ReadByte = rbuf[0] & 0x0F;
  CC1101_status_state = (rbuf[0] >> 4) & 0x0F;
}

void SPIWriteBurstReg(uint8_t spi_instr, uint8_t *pArr, uint8_t len)
{
  // Use static buffer to avoid stack overflow on ESP8266 (stack ~4KB only)
  // VLA (variable-length array) on stack was causing silent memory corruption
  // NOTE: Static buffer is safe in this single-threaded application where SPI
  // operations are serialized and never called from interrupt context.
  // SPI transactions are protected by beginTransaction() which disables interrupts.
  static uint8_t tbuf[MAX_SPI_BURST_SIZE + 1];
  uint8_t i = 0;

  // Bounds check to prevent buffer overflow
  if (len > MAX_SPI_BURST_SIZE)
  {
    echo_debug(1, "[ERROR] SPI burst write too large (%u > %u)\n", (unsigned)len, (unsigned)MAX_SPI_BURST_SIZE);
    return;
  }

  // Feed watchdog before long operations to prevent timeout
  if (len > 64)
  {
    FEED_WDT();
  }

  tbuf[0] = spi_instr | WRITE_BURST;
  for (i = 0; i < len; i++)
  {
    tbuf[i + 1] = pArr[i];
    // echo_debug(debug_out,"SPI_arr_write: 0x%02X\n", tbuf[i+1]);
  }
  wiringPiSPIDataRW(0, tbuf, len + 1);
  CC1101_status_FIFO_FreeByte = tbuf[len] & 0x0F;
  CC1101_status_state = (tbuf[len] >> 4) & 0x0F;
}

/*---------------------------[CC1100-command strobes]----------------------------*/
#define SRES 0x30    // Reset chip
#define SFSTXON 0x31 // Enable/calibrate freq synthesizer
#define SXOFF 0x32   // Turn off crystal oscillator.
#define SCAL 0x33    // Calibrate freq synthesizer & disable
#define SRX 0x34     // Enable RX.
#define STX 0x35     // Enable TX.
#define SIDLE 0x36   // Exit RX / TX
#define SAFC 0x37    // AFC adjustment of freq synthesizer
#define SWOR 0x38    // Start automatic RX polling sequence
#define SPWD 0x39    // Enter pwr down mode when CSn goes hi
#define SFRX 0x3A    // Flush the RX FIFO buffer.
#define SFTX 0x3B    // Flush the TX FIFO buffer.
#define SWORRST 0x3C // Reset real time clock.
#define SNOP 0x3D    // No operation.
/*----------------------------[END command strobes]------------------------------*/
void CC1101_CMD(uint8_t spi_instr)
{
  uint8_t tbuf[1] = {0};
  tbuf[0] = spi_instr | WRITE_SINGLE_BYTE;
  // echo_debug(debug_out,"SPI_data: 0x%02X\n", tbuf[0]);
  wiringPiSPIDataRW(0, tbuf, 1);
  CC1101_status_state = (tbuf[0] >> 4) & 0x0F;
}

void echo_cc1101_version(void);
void show_cc1101_registers_settings(void);

//---------------[CC1100 reset function]-----------------------
// Reset CC1101 via software reset strobe command (per datasheet §19.1)
void cc1101_reset(void)
{
  CC1101_CMD(SRES); // Send software reset strobe
  delay(1);         // Wait 1ms for chip to reset properly
  CC1101_CMD(SFTX); // Flush TX FIFO - required for proper interrupt handling
  CC1101_CMD(SFRX); // Flush RX FIFO - required for proper interrupt handling
}

void setMHZ(float mhz)
{
  byte freq2 = 0;
  byte freq1 = 0;
  byte freq0 = 0;

  // Serial.printf("%.4f Mhz : ", mhz);

  for (bool i = 0; i == 0;)
  {
    if (mhz >= 26)
    {
      mhz -= 26;
      freq2 += 1;
    }
    else if (mhz >= 0.1015625)
    {
      mhz -= 0.1015625;
      freq1 += 1;
    }
    else if (mhz >= 0.00039675)
    {
      mhz -= 0.00039675;
      freq0 += 1;
    }
    else
    {
      i = 1;
    }
  }
  if (freq0 > 255)
  {
    freq1 += 1;
    freq0 -= 256;
  }

  /*
  Serial.printf("FREQ2=0x%02X ", freq2);
  Serial.printf("FREQ1=0x%02X ", freq1);
  Serial.printf("FREQ0=0x%02X ", freq0);
  Serial.printf("\n");
  */

  halRfWriteReg(FREQ2, freq2);
  halRfWriteReg(FREQ1, freq1);
  halRfWriteReg(FREQ0, freq0);
}

void cc1101_configureRF_0(float freq)
{
  RF_config_u8 = 0;
  //
  // RF settings for CC1101 - RADIAN protocol (Itron EverBlu)
  //
  // GDO2: configured here as the TX FIFO threshold signal (IOCFG2_TX_FIFO_THR = 0x02)
  // for the transmit phase. receive_radian_frame() temporarily switches IOCFG2 to the
  // RX FIFO threshold / end-of-packet signal (0x01) during reception and the next TX
  // phase restores 0x02. When GDO2 is not physically wired, the output is unused and
  // both phases fall back to SPI polling (TXBYTES on TX, RXBYTES on RX).
  halRfWriteReg(IOCFG2, IOCFG2_TX_FIFO_THR);      // GDO2: TX FIFO at/above threshold
  halRfWriteReg(IOCFG0, IOCFG0_SYNC_WORD_DETECT); // GDO0: Sync word detection
  halRfWriteReg(FIFOTHR, FIFOTHR_FIFO_THR_25_40); // FIFO thresholds: TX=25 bytes, RX=40 bytes
  halRfWriteReg(SYNC1, SYNC1_PATTERN_55);         // Sync word MSB: 0x55
  halRfWriteReg(SYNC0, SYNC0_PATTERN_00);         // Sync word LSB: 0x00

  halRfWriteReg(PKTCTRL1, PKTCTRL1_NO_ADDR_CHECK); // No address check
  halRfWriteReg(PKTCTRL0, PKTCTRL0_FIXED_LENGTH);  // Fixed packet length
  halRfWriteReg(FSCTRL1, FSCTRL1_FREQ_IF);         // Frequency synthesizer IF

  setMHZ(freq); // Configure frequency using helper function

  halRfWriteReg(MDMCFG4, MDMCFG4_RX_BW_270KHZ);        // RX bandwidth: 270 kHz
  halRfWriteReg(MDMCFG3, MDMCFG3_DRATE_2_4KBPS);       // Data rate: 2.4 kbps
  halRfWriteReg(MDMCFG2, MDMCFG2_2FSK_16_16_SYNC);     // 2-FSK, 16/16 sync bits
  halRfWriteReg(MDMCFG1, MDMCFG1_NUM_PREAMBLE_2);      // Preamble: 2 bytes
  halRfWriteReg(MDMCFG0, MDMCFG0_CHANSPC_25KHZ);       // Channel spacing: 25 kHz
  halRfWriteReg(DEVIATN, DEVIATN_5_157KHZ);            // Deviation: 5.157 kHz
  halRfWriteReg(MCSM1, MCSM1_CCA_ALWAYS_IDLE);         // CCA always, idle on exit
  halRfWriteReg(MCSM0, MCSM0_FS_AUTOCAL_IDLE_TO_RXTX); // Auto-calibrate on IDLE→RX/TX
  halRfWriteReg(FOCCFG, FOCCFG_FOC_4K_2K);             // Frequency offset compensation
  halRfWriteReg(BSCFG, BSCFG_BS_PRE_KI_2);             // Bit synchronization
  // Select AGCCTRL2 based on RX_ATTENUATION_DB (non-ESPHome) or _rx_attenuation_db (ESPHome)
#ifdef USE_ESPHOME
  const int att_db = _rx_attenuation_db;
#else
#ifndef RX_ATTENUATION_DB
#define RX_ATTENUATION_DB 0
#endif
  const int att_db = RX_ATTENUATION_DB;
#endif
  uint8_t agcctrl2_val;
  if (att_db >= 18)      { agcctrl2_val = AGCCTRL2_ATT_18DB; }
  else if (att_db >= 12) { agcctrl2_val = AGCCTRL2_ATT_12DB; }
  else if (att_db >= 6)  { agcctrl2_val = AGCCTRL2_ATT_6DB;  }
  else                   { agcctrl2_val = AGCCTRL2_ATT_0DB;  }
  halRfWriteReg(AGCCTRL2, agcctrl2_val);                 // AGC: balanced 33 dB target + optional LNA limit
  halRfWriteReg(AGCCTRL1, AGCCTRL1_DEFAULT);           // AGC: default
  halRfWriteReg(AGCCTRL0, AGCCTRL0_FILTER_16);         // AGC: 16 samples
  halRfWriteReg(WORCTRL, WORCTRL_WOR_RES_1_8);         // Wake-on-radio
  halRfWriteReg(FREND1, FREND1_LNA_CURRENT);           // Front-end RX config
  // Note: Static FSCAL3/2/1/0 register writes removed - automatic calibration handles these dynamically
  halRfWriteReg(TEST2, TEST2_RX_LOW_DATA_RATE); // Test settings for low data rate
  halRfWriteReg(TEST1, TEST1_RX_LOW_DATA_RATE); // Test settings for low data rate
  halRfWriteReg(TEST0, TEST0_RX_LOW_DATA_RATE); // Test settings for low data rate

  SPIWriteBurstReg(PATABLE_ADDR, PA, 8);
}

bool cc1101_init(float freq)
{
#ifdef USE_ESPHOME
  if (GET_GDO0_PIN() < 0)
  {
    LOG_E("everblu_meter", "GDO0 pin is not configured; call cc1101_set_gdo0_pin() before cc1101_init()");
    return false;
  }
#endif

  pinMode(GET_GDO0_PIN(), INPUT_PULLUP);

  // GDO2 is optional; configure as input only when a pin is assigned.
  if (GET_GDO2_PIN() >= 0)
  {
    // Use a pull-up (matching GDO0) so a disconnected/miswired GDO2 reads HIGH. A HIGH
    // GDO2 means "TX FIFO at/above threshold - do not feed", which makes the fault fail
    // loudly and quickly via the interrogation-frame gate timeout (see _gdo2_stuck_timeouts)
    // instead of silently driving the FIFO. ESP8266 has internal pull-ups on every GPIO
    // except GPIO16; ESP32 supports INPUT_PULLUP on all input-capable pins.
    pinMode(GET_GDO2_PIN(), INPUT_PULLUP);
    echo_debug(debug_out, "[CC1101] GDO2 pin %d configured as FIFO threshold input (pull-up)\n", GET_GDO2_PIN());
  }

  // Initialize SPI transport for CC1101 communication.
  // Standalone builds configure Arduino SPI here at 500 kHz.
  // ESPHome builds ignore the requested speed and use the SPIDevice rate.
  if ((wiringPiSPISetup(0, 500000)) < 0)
  {
    LOG_E("everblu_meter", "Failed to initialize SPI bus - check CC1101 wiring and connections");
    return false;
  }

  cc1101_reset();
  delay(1);

  // Verify CC1101 is present by reading version register
  uint8_t partnum = halRfReadReg(PARTNUM_ADDR);
  uint8_t version = halRfReadReg(VERSION_ADDR);

  // Check if version register returns a valid value (not 0x00 or 0xFF)
  // PARTNUM may be 0x00 on some variants, so we rely mainly on VERSION
  if (version == 0x00 || version == 0xFF)
  {
    LOG_E("everblu_meter", "CC1101 radio not responding (PARTNUM: 0x%02X, VERSION: 0x%02X)", partnum, version);
    LOG_E("everblu_meter", "Check: 1) Wiring connections 2) 3.3V power supply 3) SPI pins");
    return false;
  }

  // Print the detection banner only once to avoid flooding logs during scans
  static bool s_reported_ok = false;
  if (!s_reported_ok)
  {
    LOG_I("everblu_meter", "Radio found OK (PARTNUM: 0x%02X, VERSION: 0x%02X)", partnum, version);
    s_reported_ok = true;
  }

  cc1101_configureRF_0(freq);

  // Perform manual calibration after configuration
  // This calibrates the frequency synthesizer for the current frequency
  CC1101_CMD(SIDLE); // Must be in IDLE state to calibrate
  CC1101_CMD(SCAL);  // Calibrate frequency synthesizer and turn it off
  delay(5);          // Wait for calibration to complete (typically <1ms, but we add margin)

  echo_debug(debug_out, "[CC1101] Frequency synthesizer calibrated for %.6f MHz\n", freq);

  // One-time GDO2 wiring self-test. With IOCFG2 in TX-FIFO-threshold mode (0x02, set by
  // cc1101_configureRF_0) GDO2 must read LOW with an empty TX FIFO and HIGH once the FIFO
  // is filled past the 25-byte threshold. If it does not toggle across that known
  // transition, GDO2 is almost certainly miswired / on the wrong GPIO / not connected.
  // Runs once per boot (skipped on the repeated cc1101_init() calls made by frequency
  // scans). A failure increments the same diagnostic counter as the runtime stuck-HIGH
  // detection, so the fault surfaces in telemetry from boot without waiting for a read.
  if (GET_GDO2_PIN() >= 0)
  {
    static bool s_gdo2_selftest_done = false;
    if (!s_gdo2_selftest_done)
    {
      s_gdo2_selftest_done = true;
      halRfWriteReg(IOCFG2, IOCFG2_TX_FIFO_THR); // ensure GDO2 = TX FIFO threshold signal
      CC1101_CMD(SFTX);                          // empty TX FIFO -> GDO2 expected LOW
      delayMicroseconds(50);
      bool low_when_empty = (digitalRead(GET_GDO2_PIN()) == LOW);
      uint8_t selftest_buf[40] = {0};            // > 25-byte threshold -> GDO2 expected HIGH
      SPIWriteBurstReg(TX_FIFO_ADDR, selftest_buf, sizeof(selftest_buf));
      delayMicroseconds(50);
      bool high_when_filled = (digitalRead(GET_GDO2_PIN()) == HIGH);
      CC1101_CMD(SFTX);                          // restore empty FIFO for normal operation
      if (low_when_empty && high_when_filled)
      {
        echo_debug(debug_out, "[CC1101] GDO2 self-test passed (LOW when empty / HIGH when filled)\n");
      }
      else
      {
        _gdo2_stuck_timeouts++;
        LOG_W("everblu_meter",
              "GDO2 self-test FAILED (empty read=%s, filled read=%s; expected LOW then HIGH) - check GDO2 wiring/pin. Reads may fail until fixed; opt out via DISABLE_GDO2_FIFO_MANAGEMENT / disable_gdo2_fifo_management to use legacy SPI polling.",
              low_when_empty ? "LOW" : "HIGH", high_when_filled ? "HIGH" : "LOW");
      }
    }
  }

  // Put radio into RX listening mode so it can receive meter data
  cc1101_rec_mode();

  return true;
}

int8_t cc1100_rssi_convert2dbm(uint8_t Rssi_dec)
{
  int8_t rssi_dbm;
  if (Rssi_dec >= 128)
  {
    rssi_dbm = ((Rssi_dec - 256) / 2) - 74; // rssi_offset via datasheet
  }
  else
  {
    rssi_dbm = ((Rssi_dec) / 2) - 74;
  }
  return rssi_dbm;
}

/* configure cc1101 in receive mode */
void cc1101_rec_mode(void)
{
  uint8_t marcstate;
  CC1101_CMD(SIDLE);       // sets to idle first. must be in
  CC1101_CMD(SRX);         // writes receive strobe (receive mode)
  marcstate = 0xFF;        // set unknown/dummy state value
  // Bounded wait for the radio to reach an RX state (0x0D/0x0E/0x0F).
  // Without a timeout a wedged radio state (e.g. 0x11 RXFIFO_OVERFLOW) would
  // spin here forever while feeding the watchdog - hanging the whole firmware
  // with no reboot. Cap the wait and attempt a FIFO flush + re-strobe to
  // recover; if that still fails, return so the caller's GDO0 wait times out
  // gracefully instead of hanging.
  uint16_t spin = 0;
  const uint16_t kMaxSpin = 20000; // ~50-100ms of tight SPI polling
  bool recovered_once = false;
  while ((marcstate != 0x0D) && (marcstate != 0x0E) && (marcstate != 0x0F)) // 0x0D = RX
  {
    marcstate = halRfReadReg(MARCSTATE_ADDR); // read out state of cc1100 to be sure in RX
    FEED_WDT();                               // Avoid soft WDT while waiting for RX state
    if (++spin >= kMaxSpin)
    {
      if (!recovered_once)
      {
        // First timeout: try to unwedge the radio (flush RX FIFO, re-strobe RX).
        recovered_once = true;
        spin = 0;
        echo_debug(1, "[CC1101] WARNING: radio stuck in state 0x%02X while entering RX - flushing and retrying\n", marcstate & 0x1F);
        CC1101_CMD(SIDLE);
        CC1101_CMD(SFRX); // flush RX FIFO (clears RXFIFO_OVERFLOW)
        CC1101_CMD(SRX);
        marcstate = 0xFF;
        continue;
      }
      // Second timeout: give up so the caller can fail this read gracefully.
      echo_debug(1, "[CC1101] ERROR: radio failed to enter RX (last state 0x%02X) - aborting receive\n", marcstate & 0x1F);
      return;
    }
  }
}

void echo_cc1101_version(void)
{
  echo_debug(debug_out, "CC1101 Partnumber: 0x%02X\n", halRfReadReg(PARTNUM_ADDR));
  echo_debug(debug_out, "CC1101 Version != 00 or 0xFF  : 0x%02X\n", halRfReadReg(VERSION_ADDR)); // != 00 or 0xFF
}

#define CFG_REGISTER 0x2F // 47 registers
void show_cc1101_registers_settings(void)
{
  uint8_t config_reg_verify[CFG_REGISTER], Patable_verify[8];
  uint8_t i;

  memset(config_reg_verify, 0, CFG_REGISTER);
  memset(Patable_verify, 0, 8);

  SPIReadBurstReg(0, config_reg_verify, CFG_REGISTER); // reads all 47 config register from cc1100	"359.63us"
  SPIReadBurstReg(PATABLE_ADDR, Patable_verify, 8);    // reads output power settings from cc1100	"104us"

  echo_debug(debug_out, "Config Register in hex:\n");
  echo_debug(debug_out, " 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");
  for (i = 0; i < CFG_REGISTER; i++) // showes rx_buffer for debug
  {
    echo_debug(debug_out, "%02X ", config_reg_verify[i]);

    if (i == 15 || i == 31 || i == 47 || i == 63) // just for beautiful output style
    {
      echo_debug(debug_out, "\n");
    }
  }
  echo_debug(debug_out, "\n");
  echo_debug(debug_out, "PaTable:\n");

  for (i = 0; i < 8; i++) // showes rx_buffer for debug
  {
    echo_debug(debug_out, "%02X ", Patable_verify[i]);
  }
  echo_debug(debug_out, "\n");
}

uint8_t is_look_like_radian_frame(uint8_t *buffer, size_t len)
{
  int ret;
  ret = FALSE;
  for (size_t i = 0; i < len; i++)
  {
    if (buffer[i] == 0xFF)
      ret = TRUE;
  }

  return ret;
}

//-----------------[check if Packet is received]-------------------------
uint8_t cc1101_check_packet_received(void)
{
  uint8_t rxBuffer[100];
  uint8_t l_nb_byte;
  int8_t l_Rssi_dbm;
  uint8_t l_lqi, l_freq_est, pktLen;
  pktLen = 0;
  if (digitalRead(GET_GDO0_PIN()) == TRUE)
  {
    // Read RSSI immediately while signal is present (carrier active)
    // RSSI register needs to be sampled during packet reception for accuracy
    l_Rssi_dbm = cc1100_rssi_convert2dbm(halRfReadReg(RSSI_ADDR));

    bool buffer_overflow = false;
    while (digitalRead(GET_GDO0_PIN()) == TRUE)
    {
      delay(2); // Reduced from 5ms to 2ms for faster FIFO reading (prevents overflow)

      // Check for FIFO overflow (bit 7 of RXBYTES register)
      uint8_t rxbytes_reg = halRfReadReg(RXBYTES_ADDR);
      if (rxbytes_reg & 0x80)
      {
        echo_debug(1, "[ERROR] RX FIFO overflow detected - data corrupted\n");
        CC1101_CMD(SFRX); // Flush RX FIFO to recover
        return FALSE;
      }

      l_nb_byte = rxbytes_reg & RXBYTES_MASK;

      // Bounds check before reading to prevent buffer overflow
      if ((l_nb_byte) && ((pktLen + l_nb_byte) <= 100))
      {
        SPIReadBurstReg(RX_FIFO_ADDR, &rxBuffer[pktLen], l_nb_byte); // Pull data
        pktLen += l_nb_byte;
      }
      else if (l_nb_byte && ((pktLen + l_nb_byte) > 100))
      {
        echo_debug(1, "[ERROR] Would overflow rxBuffer (pktLen=%u + l_nb_byte=%u > 100)\n", pktLen, l_nb_byte);
        buffer_overflow = true;
        break;
      }
    }

    // Read LQI and FREQEST only if packet completed normally (GDO0 went low)
    // If we exited via buffer overflow, GDO0 may still be high and these registers
    // are not yet latched, so reading them would give incorrect values.
    if (buffer_overflow)
    {
      echo_debug(1, "[ERROR] Buffer overflow - discarding incomplete packet\n");
      CC1101_CMD(SFRX); // Flush RX FIFO to recover
      return FALSE;
    }
    // These registers are latched at end-of-packet and contain final quality metrics
    l_lqi = halRfReadReg(LQI_ADDR);
    l_freq_est = halRfReadReg(FREQEST_ADDR);

    if (is_look_like_radian_frame(rxBuffer, pktLen))
    {
      echo_debug(debug_out, "[CC1101] Packet looks like RADIAN frame");
      echo_debug(debug_out, "[CC1101] bytes=%u rssi=%d lqi=%u F_est=%d\n", pktLen, l_Rssi_dbm, l_lqi & 0x7F, (int8_t)l_freq_est);
      show_in_hex_one_line(rxBuffer, pktLen);
      // show_in_bin(rxBuffer,l_nb_byte);
    }
    else
    {
      echo_debug(debug_out, ".");
    }
    fflush(stdout);
    return TRUE;
  }
  return FALSE;
}

uint8_t cc1101_wait_for_packet(int milliseconds)
{
  int i;
  for (i = 0; i < milliseconds; i++)
  {
    delay(1); // in ms
    if (i % 100 == 0)
      FEED_WDT(); // Feed watchdog every 100ms
    // echo_cc1101_MARCSTATE();
    if (cc1101_check_packet_received()) // delay till system has data available
    {
      return TRUE;
    }
    else if (i == milliseconds - 1)
    {
      // echo_debug(debug_out,"no packet received!\n");
      return FALSE;
    }
  }
  return TRUE;
}

// Diagnostic: the RADIAN frame length is not assumed. Scan every candidate
// total length L and report where a CRC-16/KERMIT trailer actually closes.
// The authoritative convention (proven against the reference Make_Radian_Master_req
// and the wiki's known-good master frame) is that the CRC covers bytes [0..L-3],
// i.e. it INCLUDES the length byte, and the 2 CRC bytes sit at [L-2],[L-1].
// For comparison it also tests the legacy convention that skips byte 0 (which is
// what validate_radian_crc currently does) and both trailer byte orders, so a
// clean capture tells us both the true frame length and the correct convention.
static void crc_boundary_search(const uint8_t *buf, int size)
{
  if (buf == NULL || size < 6)
  {
    echo_debug(debug_out, "[CRC-SCAN] Only %d decoded bytes - too few to search\n", size);
    return;
  }

  int matches = 0;
  for (int L = 6; L <= size; L++)
  {
    const uint16_t trailer_be = ((uint16_t)buf[L - 2] << 8) | buf[L - 1];
    const uint16_t trailer_le = ((uint16_t)buf[L - 1] << 8) | buf[L - 2];
    const uint16_t crc_incl = radian_crc_kermit(&buf[0], (size_t)(L - 2)); // includes length byte [0]
    const uint16_t crc_excl = radian_crc_kermit(&buf[1], (size_t)(L - 3)); // legacy: skips byte [0]

    if (crc_incl == trailer_be)
      echo_debug(debug_out, "[CRC-SCAN] MATCH len=%d  incl-byte0  BE-trailer  (len byte[0]=%u)\n", L, buf[0]);
    else if (crc_incl == trailer_le)
      echo_debug(debug_out, "[CRC-SCAN] MATCH len=%d  incl-byte0  LE-trailer  (len byte[0]=%u)\n", L, buf[0]);
    else if (crc_excl == trailer_be)
      echo_debug(debug_out, "[CRC-SCAN] MATCH len=%d  legacy(excl-byte0)  BE-trailer  (len byte[0]=%u)\n", L, buf[0]);
    else if (crc_excl == trailer_le)
      echo_debug(debug_out, "[CRC-SCAN] MATCH len=%d  legacy(excl-byte0)  LE-trailer  (len byte[0]=%u)\n", L, buf[0]);
    else
      continue;
    matches++;
  }

  if (matches == 0)
    echo_debug(debug_out, "[CRC-SCAN] No CRC boundary in %d decoded bytes (frame corrupt, or longer than captured)\n", size);
  else
    echo_debug(debug_out, "[CRC-SCAN] %d candidate boundary/boundaries found across %d decoded bytes\n", matches, size);
}

static bool validate_radian_crc(const uint8_t *decoded_buffer, size_t size)
{
  if (size < 4)
  {
    echo_debug(1, "[ERROR] Decoded frame too small for CRC validation (size=%u)\n", size);
    return false;
  }

  const bool crc_ok = radian_validate_crc(decoded_buffer, size);
  if (!crc_ok)
  {
    echo_debug(1, "[ERROR] RADIAN CRC validation failed - discarding frame\n");
    echo_debug(1, "[DEBUG] Frame bytes [0-31]: ");
    show_in_hex_one_line(decoded_buffer, (size < 32) ? size : 32);
    return false;
  }

  return crc_ok;
}

struct tmeter_data parse_meter_report(uint8_t *decoded_buffer, uint8_t size)
{
  struct tmeter_data data;
  memset(&data, 0, sizeof(data)); // Initialize all fields to zero

  struct radian_primary_data primary;
  if (!radian_parse_primary_data(decoded_buffer, size, &primary))
  {
    if (size < 30)
    {
      echo_debug(1, "[ERROR] Buffer too small for meter data (size=%d, need>=30)\n", size);
    }
    echo_debug(1, "[ERROR] Invalid primary meter fields - discarding frame\n");
    return data;
  }

  // Extract volume using proper uint32_t handling to prevent overflow
  // Byte order is LSB first: [18]=LSB, [19], [20], [21]=MSB
  data.volume = (int)primary.volume;
  data.reads_counter = primary.reads_counter;
  data.battery_left = primary.battery_left;
  data.time_start = primary.time_start;
  data.time_end = primary.time_end;
  data.history_available = primary.history_available;

  // Meter real-time clock and identifier string, decoded per the RADIAN
  // reference field map (display_meter_report). Logged for diagnostics; these
  // extras never gate acceptance of the reading.
  if (primary.clock_valid)
  {
    echo_debug(debug_out,
               "[CC1101] Meter clock %02u/%02u/20%02u %02u:%02u:%02u type='%s'\n",
               primary.clock_day, primary.clock_month, primary.clock_year,
               primary.clock_hour, primary.clock_minute, primary.clock_second,
               primary.meter_type);
    snprintf(data.meter_time, sizeof(data.meter_time), "20%02u-%02u-%02u %02u:%02u:%02u",
             primary.clock_year, primary.clock_month, primary.clock_day,
             primary.clock_hour, primary.clock_minute, primary.clock_second);
  }
  snprintf(data.meter_type, sizeof(data.meter_type), "%s", primary.meter_type);

  // Extract extended data if buffer is large enough
  if (size >= 49)
  { // Need at least 49 bytes to safely access decoded_buffer[48]
    // echo_debug(1,"Num %u %u Mois %uh-%uh ",decoded_buffer[48], decoded_buffer[31],decoded_buffer[44],decoded_buffer[45]);
    // Values already validated by radian_parse_primary_data.
  }
  else
  {
    echo_debug(1, "[WARN] Buffer size %d < 49, extended data unavailable\n", size);
  }

  // Extract 12 months of historical volume data if buffer is large enough
  // Historical data is stored as consecutive uint32_t values (LSB first) starting at byte 70
  // Each represents the total volume reading at the end of that month
  // Buffer is typically 120 bytes, which gives us 12 complete historical values (70 + 12*4 = 118)
  // Index 0 = oldest month (12 months ago), Index 11 = most recent month boundary
  if (size >= 118)
  { // Need at least 118 bytes for 12 complete historical values
    // Determine how many complete 4-byte values we can safely read
    int available_bytes = size - 70;
    int max_values = available_bytes / 4;
    int num_values = (max_values < 13) ? max_values : 13; // Cap at 13 in case future meters send more

    if (num_values > 0)
    {
      data.history_available = true;
      // Remove leading newline so ESPHome logs stay single-line per entry
      echo_debug(debug_out, "[CC1101] Extracting historical data from buffer (size=%d):\n", size);
      echo_debug(debug_out, "[CC1101] Starting at byte 70: %d bytes available, %d complete values\n",
                 available_bytes, num_values);

      for (int i = 0; i < num_values; i++)
      {
        int offset = 70 + (i * 4); // Start at byte 70, each value is 4 bytes

        // Safety check - ensure we don't read past buffer
        if (offset + 3 >= size)
        {
          echo_debug(debug_out, "  Stopping at value %d (would read past buffer at byte %d)\n", i, offset);
          break;
        }

        data.history[i] = ((uint32_t)decoded_buffer[offset]) |
                          ((uint32_t)decoded_buffer[offset + 1] << 8) |
                          ((uint32_t)decoded_buffer[offset + 2] << 16) |
                          ((uint32_t)decoded_buffer[offset + 3] << 24);
        echo_debug(debug_out, "  Month -%02d [bytes %3d-%3d]: %02X %02X %02X %02X = %u L\n",
                   num_values - i, offset, offset + 3,
                   decoded_buffer[offset], decoded_buffer[offset + 1],
                   decoded_buffer[offset + 2], decoded_buffer[offset + 3],
                   data.history[i]);
      }

      // Fill remaining slots with zeros
      for (int i = num_values; i < 13; i++)
      {
        data.history[i] = 0;
      }

      echo_debug(debug_out, "[CC1101] Extracted %d historical values: %u L (oldest) → %u L (newest)\n",
                 num_values, data.history[0], data.history[num_values - 1]);

      // --- Sanity checks on historical data ---------------------------------
      // We only perform light-touch validation here to catch clearly bogus
      // frames (e.g. huge jump to billions of litres on a domestic meter).
      // If the history fails validation we *keep* the main reading (liters,
      // battery, window, etc.) but mark history as unavailable so that the
      // caller can still use the primary data while ignoring the corrupted
      // historical block.

      bool history_ok = true;

      // 1) Volumes must be non-decreasing (meter is cumulative)
      for (int i = 1; i < num_values; i++)
      {
        if (data.history[i] < data.history[i - 1])
        {
          echo_debug(1, "[ERROR] Historical volume decreased at index %d (%u -> %u) - marking history invalid\n",
                     i, data.history[i - 1], data.history[i]);
          echo_debug(1, "[DEBUG] Historical bytes [%d-%d]: %02X %02X %02X %02X (index %d)\n",
                     66 + (i * 4), 66 + (i * 4) + 3,
                     decoded_buffer[66 + (i * 4)], decoded_buffer[66 + (i * 4) + 1],
                     decoded_buffer[66 + (i * 4) + 2], decoded_buffer[66 + (i * 4) + 3], i);
          history_ok = false;
          break;
        }
      }

      // 2) Guard against absurd per-month usage spikes. We use a very generous
      //     upper bound so that only clearly corrupt frames are rejected.
      if (history_ok && num_values > 1)
      {
        const uint32_t MAX_REASONABLE_USAGE = 500000000UL; // 500M L/month
        for (int i = 1; i < num_values; i++)
        {
          uint32_t delta = data.history[i] - data.history[i - 1];
          if (delta > MAX_REASONABLE_USAGE)
          {
            echo_debug(1, "[ERROR] Historical usage spike at index %d (delta=%u L) - marking history invalid\n",
                       i, delta);
            history_ok = false;
            break;
          }
        }
      }

      // 3) Cross-check newest historical value against current meter reading
      //    (if available). History should never greatly exceed the current
      //    reading because each entry is a past snapshot of the same counter.
      if (history_ok && data.volume > 0 && num_values > 0)
      {
        uint32_t newest = data.history[num_values - 1];
        if (newest > (uint32_t)data.volume)
        {
          uint32_t diff = newest - data.volume;
          // Allow a small tolerance for off-by-one / rounding, but reject
          // obviously impossible values.
          const uint32_t MAX_FORWARD_TOLERANCE = 1000000UL; // 1M unit tolerance (liters for water, or internal units for gas)
          if (diff > MAX_FORWARD_TOLERANCE)
          {
            echo_debug(1, "[ERROR] Newest history value (%u) exceeds current volume (%u) by %u units - marking history invalid\n",
                       newest, data.volume, diff);
            history_ok = false;
          }
        }
      }

      if (!history_ok)
      {
        data.history_available = false;
        memset(data.history, 0, sizeof(data.history));
        echo_debug(1, "[WARN] Discarded corrupted historical block while keeping primary meter fields\n");
      }

      // 4) Plausibility guard on the current reading versus its own history.
      //    Reject the whole frame when the implied current-month usage exceeds
      //    100x the largest historical monthly usage - a corrupted current
      //    volume shows up as an absurd jump relative to the meter's history.
      //    Skipped automatically when history is insufficient/unreliable (see
      //    radian_reading_within_history_bounds()).
      if (history_ok &&
          !radian_reading_within_history_bounds((uint32_t)data.volume, data.history, num_values, 100UL))
      {
        echo_debug(1, "[ERROR] Current-month usage exceeds 100x the largest historical monthly usage - discarding reading\n");
        memset(&data, 0, sizeof(data));
        return data;
      }
    }
    else
    {
      data.history_available = false;
      echo_debug(1, "[WARN] Not enough data for historical values (only %d bytes from offset 70)\n", available_bytes);
    }
  }
  else
  {
    data.history_available = false;
    echo_debug(debug_out, "[CC1101] Buffer size %d < 118, historical data unavailable\n", size);
  }

  return data;
}

// Function: decode_4bitpbit_serial
// Description: Decodes RADIAN protocol's 4-bit-per-bit serial encoding to extract raw data bytes.
//
// RADIAN Protocol Serial Encoding:
// ---------------------------------
// Each data byte is transmitted with:
// - 1 start bit (0)
// - 8 data bits (LSB first)
// - 3 stop bits (111)
//
// Additionally, each bit is oversampled 4x for noise immunity:
// - Logical '1' → transmitted as 0xF0 (1111 0000 in binary)
// - Logical '0' → transmitted as 0x0F (0000 1111 in binary)
//
// Example: Byte 0xA5 (10100101) would be transmitted as:
// Start bit 0 → 0x0F
// Bit 0 (1)   → 0xF0
// Bit 1 (0)   → 0x0F
// Bit 2 (1)   → 0xF0
// ...and so on
// Stop bits 111 → 0xF0 0xF0 0xF0
//
// This function:
// 1. Detects bit polarity changes to identify logical bits
// 2. Counts consecutive same-polarity samples (should be ~4 per bit)
// 3. Extracts data bits and discards start/stop bits
// 4. Reverses bit order (LSB first → MSB first for normal byte representation)
//
// Remove the start- and stop-bits in the bitstream, also decode oversampled bit 0xF0 => 1,0
// 01234567 ###01234 567###01 234567## #0123456 (# -> Start/Stop bit)
// is decoded to:
// 76543210 76543210 76543210 76543210
// Note: this wrapper always passes MAX_DECODED_SIZE (200) as the output-buffer
// limit, so decoded_buffer must be at least 200 bytes (see the static
// meter_data[200] caller); the decode stops early if that limit is reached.
//
// The core bit-recovery algorithm lives in radian_decode_4bitpbit()
// (src/core/radian_decoder.cpp) so that a single, platform-neutral
// implementation is shared by the firmware and the native hex_decoder tool
// (see issue #118). This function is a thin Arduino-side wrapper that keeps
// the firmware-specific concerns - watchdog feeding and debug diagnostics -
// around the pure decode.
uint8_t decode_4bitpbit_serial(uint8_t *rxBuffer, int l_total_byte, uint8_t *decoded_buffer)
{
  // Maximum decoded buffer size (matches the static meter_data[200] caller
  // buffer; a conservative estimate of input bytes / 4).
  const int MAX_DECODED_SIZE = 200;

  // Feed the watchdog before and after the decode. The decode itself is a
  // tight in-memory loop that completes well within the watchdog window for
  // the ~1000-byte oversampled frames seen in practice, so a single feed on
  // each side is sufficient to keep the SDK/WiFi task serviced.
  FEED_WDT();

  uint8_t dest_byte_cnt =
      radian_decode_4bitpbit(rxBuffer, l_total_byte, decoded_buffer, MAX_DECODED_SIZE);

  FEED_WDT();

  if (dest_byte_cnt == 0)
  {
    // radian_decode_4bitpbit() returns 0 when the frame quality is too low
    // (too many framing errors relative to decoded byte count).
    echo_debug(debug_out, "[ERROR] Decode quality too low or empty frame - discarding\n");
  }
  else
  {
    echo_debug(debug_out, "[CC1101] Decoded %u bytes from %d raw bytes\n", dest_byte_cnt, l_total_byte);
  }

  return dest_byte_cnt;
}

/*
   Function: receive_radian_frame
   Description: Receives a RADIAN protocol frame using a two-stage sync detection approach.

   This function implements the RADIAN protocol's unique frame reception strategy:

   Stage 1: Initial Sync Detection (2.4 kbps)
   ------------------------------------------
   - Configures CC1101 to detect sync pattern 0x5550 (01010101 01010000)
   - This is the preamble/sync that precedes the actual data
   - Uses standard data rate (2.4 kbps) for reliable sync detection
   - Waits for GDO0 pin to go high (indicates sync word detected)

   Stage 2: Frame Start and Data Reception (9.6 kbps with 4x oversampling)
   -----------------------------------------------------------------------
   - Reconfigures CC1101 to detect 0xFFF0 (11111111 11110000)
   - This pattern marks the end of sync and the start bit of the first data byte
   - Switches to 4x data rate (9.6 kbps) to capture multiple samples per bit
   - Receives the full frame into the buffer
   - Uses decode_4bitpbit_serial() to extract actual data from oversampled stream

   Why 4x Oversampling (9.6 kbps for 2.4 kbps data)?
   ================================================
   The meter officially transmits at 2400 baud, but the actual demodulation requires
   4x oversampling for several critical reasons:

   1. BIT BOUNDARY IDENTIFICATION
      - At 2400 baud with 26MHz crystal, each bit is ~10,800 clock cycles
      - Phase errors and jitter can cause misalignment
      - 4x oversampling provides 4 decision points per bit
      - Decoder can interpolate correct bit boundaries

   2. NOISE AND INTERFERENCE IMMUNITY
      - 433.82 MHz ISM band has other devices (fencing, doorbells, etc.)
      - 4 samples per bit allow voting/filtering logic
      - Reduces false edge detection from RF noise
      - Improves packet error rate significantly

   3. FREQUENCY OFFSET TOLERANCE
      - Meter oscillator may have ±50 ppm tolerance
      - With 2400 baud: Each bit ~417 µs, offset could be ±20 µs per bit
      - 4x oversampling: Each sample ~104 µs, 20 µs offset is manageable
      - Decoder can track frequency drift across the frame

   4. PHASE ALIGNMENT
      - Multiple clock domains (ESP8266 26MHz vs meter oscillator)
      - Phase differences accumulate over ~1000+ bit frame
      - Extra samples allow locking to true bit edges
      - Prevents cumulative timing errors

   EMPIRICAL VALIDATION (January 1, 2026)
   ======================================
   Dredzik suggested removing 4x oversampling (native 2.4 kbps RX throughout).
   Test result: FAILED - Sync detected but frame reception timed out on all 5 attempts.

   Conclusion: 4x oversampling is NOT redundant optimization-it is essential.
   See docs/DREDZIK_IMPROVEMENTS_TEST_RESULTS.md for complete test details.

   Technical Details:
   - MDMCFG4 register controls both RX bandwidth (58 kHz) and effective data rate
   - MDMCFG3 nominally labeled "2.4 kbps" but actual rate set by MDMCFG4 value
   - decode_4bitpbit_serial() expects 4 FSK/samples per original data bit
   - Changing to 2.4 kbps RX breaks the ratio needed for proper decoding

   Meter Hardware Details:
   - Device: Itron EverBlu Cyble Enhanced
   - Serial: 2020-0123456
   - Frequency: 433.82 MHz ±0.05 ppm
   - Modulation: 2-FSK with 5 kHz shift
   - Preamble: 0x55 repeated (01010101...)
   - Sync: 0x5550 (stage 1) → 0xFFF0 (stage 2)

   References:
   - Datasheet: docs/datasheets/water_counter_wiki_maison_simon.md
   - Test Results: docs/DREDZIK_IMPROVEMENTS_TEST_RESULTS.md (Improvement #4)

   Parameters:
   - size_byte: Expected size of the decoded frame (before serial encoding)
   - rx_tmo_ms: Receive timeout in milliseconds
   - rxBuffer: Buffer to store received raw data
   - rxBuffer_size: Size of the receive buffer

   Returns:
   - Number of bytes received (raw, encoded data)
   - 0 if timeout or reception failed

   Note: The received data is 4x larger than the decoded size due to oversampling
   and needs to be processed by decode_4bitpbit_serial() to extract actual data.
*/
int receive_radian_frame(int size_byte, int rx_tmo_ms, uint8_t *rxBuffer, int rxBuffer_size)
{
  uint8_t l_byte_in_rx = 0;
  uint16_t l_total_byte = 0;
  // On-air framing per byte is 1 start + 8 data + 3 stop = 12 bits (see
  // encode2serial_1_3). The previous (8 + 3) = 11-bit estimate under-counted the
  // raw capture length, so the hard cap l_expected_bytes stopped ~60 raw bytes
  // early and the last ~4 decoded bytes (13th history month + CRC trailer) were
  // lost. (8 + 4) = 12 bits sizes the capture to cover the whole frame.
  uint16_t l_radian_frame_size_byte = ((size_byte * (8 + 4)) / 8) + 1;
  int l_tmo = 0;
  int8_t l_Rssi_dbm;
  uint8_t l_lqi, l_freq_est;

  echo_debug(debug_out, "[RX] size_byte=%d  l_radian_frame_size_byte=%d\n", size_byte, l_radian_frame_size_byte);

  if (l_radian_frame_size_byte * 4 > rxBuffer_size)
  {
    echo_debug(debug_out, "buffer too small\n");
    return 0;
  }
  CC1101_CMD(SFRX);
  // Switch GDO2 to the RX FIFO threshold / end-of-packet signal for this receive
  // phase (IOCFG2 = 0x01). The Stage 2 drain loop uses it to skip RXBYTES SPI reads
  // until the FIFO has buffered a worthwhile chunk. The next TX phase restores 0x02.
  if (GET_GDO2_PIN() >= 0)
    halRfWriteReg(IOCFG2, IOCFG2_RX_FIFO_THR_OR_EOP);
  halRfWriteReg(MCSM1, MCSM1_CCA_ALWAYS_RX);       // CCA always, RX on exit
  halRfWriteReg(MDMCFG2, MDMCFG2_2FSK_16_16_SYNC); // 2-FSK, 16/16 sync bits
  /* configure to receive beginning of sync pattern */
  halRfWriteReg(SYNC1, SYNC1_PATTERN_55);        // Sync pattern: 0x55
  halRfWriteReg(SYNC0, SYNC0_PATTERN_50);        // Sync pattern: 0x50
  halRfWriteReg(MDMCFG4, MDMCFG4_RX_BW_58KHZ);   // RX BW: 58 kHz
  halRfWriteReg(MDMCFG3, MDMCFG3_DRATE_2_4KBPS); // Data rate: 2.4 kbps
  halRfWriteReg(PKTLEN, 1);                      // Just one byte of sync pattern
  cc1101_rec_mode();

  while ((digitalRead(GET_GDO0_PIN()) == FALSE) && (l_tmo < rx_tmo_ms))
  {
    delay(1);
    l_tmo++;
    if (l_tmo % 50 == 0)
      FEED_WDT(); // Feed watchdog every 50ms
  }
  if (l_tmo < rx_tmo_ms)
  {
    echo_debug(debug_out, "[CC1101] GDO0 triggered at %dms\n", l_tmo);
  }
  else
  {
    echo_debug(debug_out, "[RX] No sync detected before timeout (meter may be asleep/out of range/wrong config)\n");
    return 0;
  }
  while ((l_byte_in_rx == 0) && (l_tmo < rx_tmo_ms))
  {
    delay(5);
    l_tmo += 5; // wait for some byte received
    FEED_WDT(); // Feed watchdog during receive wait
    l_byte_in_rx = (halRfReadReg(RXBYTES_ADDR) & RXBYTES_MASK);
    if (l_byte_in_rx)
    {
      SPIReadBurstReg(RX_FIFO_ADDR, &rxBuffer[0], l_byte_in_rx); // Pull data
      // if (debug_out)show_in_hex_one_line(rxBuffer, l_byte_in_rx);
    }
  }

  if (l_tmo < rx_tmo_ms && l_byte_in_rx > 0)
  {
    echo_debug(debug_out, "[CC1101] First sync pattern received (%d bytes)\n", l_byte_in_rx);
  }
  else
  {
    echo_debug(debug_out, "[RX] Sync detected but no payload bytes before timeout\n");
    return 0;
  }

  l_lqi = halRfReadReg(LQI_ADDR);
  l_freq_est = halRfReadReg(FREQEST_ADDR);
  l_Rssi_dbm = cc1100_rssi_convert2dbm(halRfReadReg(RSSI_ADDR));
  echo_debug(debug_out, "[CC1101] rssi=%d lqi=%u F_est=%d\n", l_Rssi_dbm, l_lqi & 0x7F, (int8_t)l_freq_est);

  fflush(stdout);

  // ========== CRITICAL BAUD RATE EXPLANATION ==========
  // The Itron EverBlu Cyble meter officially transmits at 2400 baud.
  // However, the actual implementation requires 4x oversampling (9600 baud RX)
  // for reliable frame reception. This was empirically validated on 2026-01-01.
  //
  // Why is oversampling necessary?
  // 1. Timing Margin: Provides buffer for bit boundary identification
  // 2. Noise Immunity: Multiple samples per bit enable RF noise filtering
  // 3. Frequency Tolerance: Compensates for oscillator drift/frequency offset
  // 4. Phase Alignment: Helps lock to actual bit edges despite jitter
  //
  // IMPORTANT: 8x Oversampling Does NOT Help
  // =========================================
  // Attempted 8x oversampling (19.2 kbps) on 2026-01-01, but it made results WORSE.
  // Reason: The meter transmits with 4x-oversampled encoding built-in.
  // When RX rate is 8x, we over-sample the already 4x-encoded bits,
  // causing bit boundary detection to fail (misaligned by 2x).
  //
  // This is a fundamental limitation: You can't improve 4x-encoded data
  // by sampling faster - the encoding format itself is fixed by the meter.
  // Better approaches to improve frame quality:
  // - Wider RX bandwidth (tradeoff: more noise)
  // - Better sync detection/alignment logic
  // - Improved error correction in decode_4bitpbit_serial()
  // - Adaptive frequency tracking (already implemented)
  // - Multiple consecutive reads with voting
  //
  // See: docs/DREDZIK_IMPROVEMENTS_TEST_RESULTS.md for full details
  // =================================================================

  halRfWriteReg(SYNC1, SYNC1_PATTERN_FF);              // Sync word MSB: 0xFF
  halRfWriteReg(SYNC0, SYNC0_PATTERN_F0);              // Sync word LSB: 0xF0 (frame start marker)
  halRfWriteReg(MDMCFG4, MDMCFG4_RX_BW_58KHZ_9_6KBPS); // RX BW: 58 kHz with 9.6 kbps rate (4x oversampling)
  halRfWriteReg(MDMCFG3, MDMCFG3_DRATE_2_4KBPS);       // Data rate label (controlled by MDMCFG4)
  halRfWriteReg(PKTCTRL0, PKTCTRL0_INFINITE_LENGTH);   // Infinite packet length for variable-size frames
  CC1101_CMD(SFRX);
  cc1101_rec_mode();

  // Reset timing for Stage 2 so the timeout applies only to frame-start and
  // payload reception (Stage 1 already consumed part of the rx_tmo budget).
  l_tmo = 0;
  l_total_byte = 0;
  l_byte_in_rx = 1;
  while ((digitalRead(GET_GDO0_PIN()) == FALSE) && (l_tmo < rx_tmo_ms))
  {
    delay(1);
    l_tmo++;
    if (l_tmo % 50 == 0)
      FEED_WDT(); // Feed watchdog every 50ms
  }
  if (l_tmo < rx_tmo_ms)
  {
    echo_debug(debug_out, "[CC1101] GDO0 triggered for frame start at %dms\n", l_tmo);
  }
  else
  {
    echo_debug(debug_out, "[ERROR] Timeout waiting for GDO0 (frame start)\n");
    return 0;
  }
  // Fixed-length capture sized from the expected frame (4x oversampled). This
  // reads exactly one frame's worth and returns promptly, which keeps the tight
  // ACK-then-data reply timing the meter expects. (An earlier capture-to-end
  // experiment lingered on noise and broke reads - see git history.)
  uint16_t l_expected_bytes = l_radian_frame_size_byte * 4;
  bool l_use_gdo2 = (GET_GDO2_PIN() >= 0);
  while ((l_total_byte < l_expected_bytes) && (l_tmo < rx_tmo_ms))
  {
    delay(5);
    l_tmo += 5; // wait for some byte received
    if (l_tmo % 50 == 0)
      FEED_WDT(); // Feed watchdog every 50ms during frame receive

    // GDO2 fast path (IOCFG2 = RX FIFO threshold / EOP): while GDO2 is LOW the
    // FIFO is below threshold, so skip the RXBYTES read. The final tail
    // (< threshold) never raises GDO2 under infinite packet length, so resume
    // polling once we are within one threshold of the expected total.
    if (l_use_gdo2 && digitalRead(GET_GDO2_PIN()) == LOW &&
        (l_expected_bytes - l_total_byte) > RX_FIFO_THRESHOLD_BYTES)
    {
      continue; // not enough buffered yet; skip the unnecessary RXBYTES read
    }

    l_byte_in_rx = (halRfReadReg(RXBYTES_ADDR) & RXBYTES_MASK);
    if (l_byte_in_rx)
    {
      // Do not pull more than we expect; excess bytes are noise and would skew
      // the decode. Clamp the burst to the remaining expected length.
      if (l_byte_in_rx + l_total_byte > l_expected_bytes)
        l_byte_in_rx = l_expected_bytes - l_total_byte;

      if (l_byte_in_rx > 0)
      {
        SPIReadBurstReg(RX_FIFO_ADDR, &rxBuffer[l_total_byte], l_byte_in_rx); // Pull data
        l_total_byte += l_byte_in_rx;
      }
    }
  }

  if (l_tmo < rx_tmo_ms && l_total_byte > 0)
  {
    echo_debug(debug_out, "[CC1101] Frame received successfully (%d bytes)\n", l_total_byte);
  }
  else
  {
    echo_debug(debug_out, "[ERROR] Timeout or no data received (got %d bytes)\n", l_total_byte);
    return 0;
  }

  /*stop reception*/
  CC1101_CMD(SFRX);
  CC1101_CMD(SIDLE);
  // echo_debug(debug_out,"RAW buffer");
  // show_in_hex_array(rxBuffer,l_total_byte); //16ms for 124b->682b , 7ms for 18b->99byte
  /*restore default registers */
  halRfWriteReg(MDMCFG4, MDMCFG4_RX_BW_58KHZ);    // Restore RX BW: 58 kHz, 2.4 kbps
  halRfWriteReg(MDMCFG3, MDMCFG3_DRATE_2_4KBPS);  // Restore data rate: 2.4 kbps
  halRfWriteReg(PKTCTRL0, PKTCTRL0_FIXED_LENGTH); // Restore fixed packet length
  halRfWriteReg(PKTLEN, 38);                      // Restore packet length
  halRfWriteReg(SYNC1, SYNC1_PATTERN_55);         // Restore sync word MSB: 0x55
  halRfWriteReg(SYNC0, SYNC0_PATTERN_00);         // Restore sync word LSB: 0x00
  return l_total_byte;
}

/*
   RADIAN Protocol Overview:
   ========================
   The RADIAN protocol is used by Itron EverBlu Cyble Enhanced water meters for RF communication.

   Key Protocol Characteristics:
   - Frequency: 433.82 MHz (nominal center frequency)
   - Modulation: 2-FSK (Frequency Shift Keying)
   - Data Rate: 2.4 kbps (sync detection), 9.6 kbps (data reception with 4-bit oversampling)
   - Deviation: ±5.157 kHz
   - Sync Pattern: 0x5550 (for initial sync) / 0xFFF0 (for frame start detection)
   - Packet Structure: Variable length with start/stop bits (serial-like encoding)

   Communication Sequence:
   -----------------------
   1. Wake-Up Phase (WUP):
      - Master sends ~2 seconds of 0x55 bytes to wake up the meter
      - This alerts the meter that a query is coming

   2. Interrogation Frame:
      - Master sends a 130ms interrogation command containing:
        * Meter year and serial number (identification)
        * CRC for error detection
      - Frame uses serial encoding: 1 start bit, 8 data bits, 3 stop bits

   3. Meter Response - Acknowledgement:
      - 43ms of RF noise
      - 34ms of sync pattern (0101...01)
      - 14.25ms of zeros (000...000)
      - 14ms of ones (1111...11111)
      - 83.5ms of acknowledgement data

   4. Meter Response - Data Frame:
      - 50ms of ones
      - 34ms of sync pattern (0101...01)
      - 14.25ms of zeros
      - 14ms of ones
      - 582ms of actual meter data including:
        * Water consumption in liters
        * Read counter
        * Battery life remaining (in months)
        * Wake/sleep schedule (time_start, time_end)
        * Timestamp information

   Data Encoding:
   --------------
   The RADIAN protocol uses 4-bit-per-bit oversampling:
   - Each logical '1' is transmitted as 0xF0 (1111 0000)
   - Each logical '0' is transmitted as 0x0F (0000 1111)
   - This provides noise immunity and clock recovery

   Serial Frame Format:
   - Start bit: 0 (1 bit)
   - Data: LSB first (8 bits)
   - Stop bits: 111 (3 bits)

   The decode_4bitpbit_serial() function reverses this encoding to extract the original data.
*/

/*
   scenario_releve (Reading Scenario Timeline):
   ============================================
   2000ms : WUP (Wake-Up Pattern) - continuous 0x55 bytes
   130ms  : Interrogation frame from master ______------|...............-----
   43ms   : RF noise
   34ms   : Sync pattern 0101...01
   14.25ms: Zeros 000...000
   14ms   : Ones 1111...11111
   83.5ms : Acknowledgement data
   50ms   : Ones 1111...11111
   34ms   : Sync pattern 0101...01
   14.25ms: Zeros 000...000
   14ms   : Ones 1111...11111
   582ms  : Full meter data (liters, battery, counter, schedule, etc.)

   Note: The master (this code) should normally send an acknowledgement back to the meter,
   but for read-only operation, this is not required.
*/

struct tmeter_data get_meter_data_for_meter(uint8_t meter_year, uint32_t meter_serial)
{
  // Avoid leading newline so ESPHome log doesn't emit an empty line first
  echo_debug(1, "[METER] Starting meter read sequence...\n");
  struct tmeter_data sdata;
  uint8_t marcstate = 0xFF;
  uint8_t wupbuffer[] = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
  uint8_t wup2send = 77;
  uint16_t tmo = 0;
  static uint8_t rxBuffer[1500]; // Make static to avoid stack overflow
  int rxBuffer_size;
  static uint8_t meter_data[300]; // Make static to avoid stack overflow
  uint8_t meter_data_size = 0;

  memset(&sdata, 0, sizeof(sdata));
  memset(rxBuffer, 0, sizeof(rxBuffer));     // Clear static buffer
  memset(meter_data, 0, sizeof(meter_data)); // Clear static buffer

  uint8_t txbuffer[100];
  Make_Radian_Master_req(txbuffer, meter_year, meter_serial);

  echo_debug(1, "[METER] Transmitting wake-up + interrogation (Year=%d, Serial=%lu)...\n",
             meter_year, (unsigned long)meter_serial);

  // === Critical: Reset radio state before TX ===
  // If the radio is stuck in RXFIFO_OVERFLOW (0x11) or any non-IDLE state from
  // a previous cycle, STX will be ignored. Force IDLE and flush both FIFOs.
  CC1101_CMD(SIDLE); // Force radio to IDLE (required before flushing FIFOs)
  delay(1);          // Brief settle time
  CC1101_CMD(SFRX);  // Flush RX FIFO (clears RXFIFO_OVERFLOW state)
  CC1101_CMD(SFTX);  // Flush TX FIFO (clears any stale data / TXFIFO_UNDERFLOW)
  delay(1);          // Brief settle time after flush
  echo_debug(debug_out, "[CC1101] Pre-TX reset: IDLE + FIFO flush complete\n");

  // Ensure GDO2 signals the TX FIFO threshold for this transmit phase. A previous
  // receive_radian_frame() call may have left IOCFG2 set to the RX threshold (0x01).
  if (GET_GDO2_PIN() >= 0)
    halRfWriteReg(IOCFG2, IOCFG2_TX_FIFO_THR);

  // Restore the 2.4 kbps data rate for this transmit phase. receive_radian_frame()
  // reprograms MDMCFG4 to MDMCFG4_RX_BW_58KHZ_9_6KBPS (0xF8) for its 4x-oversampled
  // RX stage, which raises the data-rate exponent to 9.6 kbps. cc1101_init() leaves
  // MDMCFG4 at the 2.4 kbps value, so the FIRST read after boot transmits correctly,
  // but every subsequent read would otherwise inherit the stale 9.6 kbps rate and
  // clock the wake-up burst out 4x too fast - draining the pre-filled TX FIFO in
  // ~47ms and triggering TXFIFO_UNDERFLOW after ~60ms instead of the full ~2s burst.
  // Only the DRATE_E (low) nibble of MDMCFG4 sets the data rate; the upper CHANBW
  // nibble is RX-only, so preserve it via read-modify-write and rewrite just the
  // low nibble to the 2.4 kbps exponent. This makes every attempt transmit at 2.4 kbps.
  // See: https://github.com/genestealer/everblu-meters-esp8266-improved/issues/127
  uint8_t mdmcfg4 = halRfReadReg(MDMCFG4);
  mdmcfg4 = (mdmcfg4 & 0xF0) | (MDMCFG4_RX_BW_270KHZ & 0x0F); // DRATE_E -> 2.4 kbps
  halRfWriteReg(MDMCFG4, mdmcfg4);               // TX data rate exponent: 2.4 kbps
  halRfWriteReg(MDMCFG3, MDMCFG3_DRATE_2_4KBPS); // TX data rate mantissa: 2.4 kbps

  halRfWriteReg(MDMCFG2, MDMCFG2_NO_PREAMBLE_SYNC);  // No preamble/sync for WUP
  halRfWriteReg(PKTCTRL0, PKTCTRL0_INFINITE_LENGTH); // Infinite packet length

  // Pre-fill FIFO to near capacity before starting TX.
  // A fuller FIFO provides much larger buffer (~186ms vs ~27ms) against
  // yield()/delay() latency in ESPHome where background tasks (WiFi, API
  // server, OTA, logger, mDNS) steal CPU time during each yield() call.
  // CC1101 TX FIFO is 64 bytes; fill up to 56 bytes (7 x 8-byte WUP buffers).
  {
    int prefill = (wup2send > 7) ? 7 : wup2send;
    for (int i = 0; i < prefill; i++)
    {
      SPIWriteBurstReg(TX_FIFO_ADDR, wupbuffer, 8);
      wup2send--;
    }
  }
  CC1101_CMD(STX);                          // sends the data store into transmit buffer over the air
  delay(10);                                // to give time for calibration
  marcstate = halRfReadReg(MARCSTATE_ADDR); // to  update 	CC1101_status_state
  echo_debug(debug_out, "MARCSTATE : raw:0x%02X  0x%02X free_byte:0x%02X sts:0x%02X sending 2s WUP...\n", marcstate, marcstate & 0x1F, CC1101_status_FIFO_FreeByte, CC1101_status_state);
  // Ensure we actually enter TX before starting the WUP/data feeding loop.
  // Sometimes after STX, the radio needs a brief extra moment to transition.
  if (CC1101_status_state != 0x02)
  {
    uint8_t spin = 0; // up to ~100ms
    while (CC1101_status_state != 0x02 && spin < 10)
    {
      FEED_WDT();
      delay(10);
      marcstate = halRfReadReg(MARCSTATE_ADDR); // refresh status to update CC1101_status_state
      spin++;
    }
  }
  while ((CC1101_status_state == 0x02) && (tmo < TX_LOOP_OUT)) // in TX
  {
    // Feed watchdog to prevent reset during long operations (every ~10ms in this loop)
    FEED_WDT();

    if (wup2send)
    {
      if (wup2send < 0xFF)
      {
        if (GET_GDO2_PIN() >= 0)
        {
          // GDO2 configured: asserts HIGH when TX FIFO >= 25 bytes (FIFOTHR_FIFO_THR_25_40).
          // Only refill when GDO2 de-asserts LOW (FIFO below threshold, >= 40 free bytes).
          // This replaces the stale CC1101_status_FIFO_FreeByte check + fixed delay(20) with
          // a real-time hardware signal, preventing underflows under ESPHome scheduler load.
          if (digitalRead(GET_GDO2_PIN()) == LOW)
          {
            // Safety guard: GDO2 LOW should mean the FIFO is below threshold (>= 40 free
            // bytes). A miswired / stuck-LOW GDO2 would otherwise let this loop write
            // unconditionally and overflow the 64-byte TX FIFO. Confirm real free space
            // via TXBYTES before writing, and abort on an existing underflow.
            uint8_t txbytes_reg = halRfReadReg(TXBYTES_ADDR);
            if (txbytes_reg & 0x80)
            {
              marcstate = halRfReadReg(MARCSTATE_ADDR); // refresh so the post-loop abort check/diagnostics are accurate
              break;                                    // TXFIFO_UNDERFLOW already occurred
            }
            if ((txbytes_reg & 0x7F) <= 56) // room for the 8-byte WUP buffer
            {
              SPIWriteBurstReg(TX_FIFO_ADDR, wupbuffer, 8);
              wup2send--;
            }
            // else: GDO2 claims below-threshold but FIFO is nearly full (stuck-LOW miswire) - skip
          }
          // else: FIFO still above threshold - skip write this iteration
        }
        else
        {
          // Fallback: use SPI status byte FIFO level (may be stale by one transaction).
          if (CC1101_status_FIFO_FreeByte <= 10)
          { // this gives 10+20ms from previous frame : 8*8/2.4k=26.6ms  time to send a wupbuffer
            delay(20);
            tmo++;
            tmo++;
          }
          SPIWriteBurstReg(TX_FIFO_ADDR, wupbuffer, 8);
          wup2send--;
        }
      }
    }
    else
    {
      // Wait for TX FIFO to drain enough to fit the 39-byte interrogation frame.
      // Only write when FIFO space is confirmed; skip and retry the outer TX loop if
      // the safety limit fires before the FIFO drains (avoids write with no headroom).
      bool fifo_ready = false;
      if (GET_GDO2_PIN() >= 0)
      {
        // With FIFOTHR_FIFO_THR_25_40: GDO2 de-asserts (LOW) when FIFO < 25 bytes,
        // guaranteeing >= 40 free bytes - safely fits the 39-byte frame.
        // See: https://github.com/genestealer/everblu-meters-esp8266-improved/issues/83
        uint8_t wait_count = 0;
        while (digitalRead(GET_GDO2_PIN()) == HIGH && wait_count < 100) // Safety limit ~500ms
        {
          delay(5);
          wait_count++;
          if (wait_count % 10 == 0)
            FEED_WDT();
        }
        // Final underflow check: if FIFO underflowed during the wait, abort TX loop.
        uint8_t txbytes_reg = halRfReadReg(TXBYTES_ADDR);
        if (txbytes_reg & 0x80)
        {
          marcstate = halRfReadReg(MARCSTATE_ADDR); // refresh so tx_aborted check below is accurate
          break;                                    // TXFIFO_UNDERFLOW already occurred
        }
        // Confirm GDO2 is actually LOW (FIFO drained), not merely timed out with FIFO still full.
        fifo_ready = (digitalRead(GET_GDO2_PIN()) == LOW);
        if (!fifo_ready && wait_count >= 100)
        {
          // GDO2 never went LOW within the safety window. With FIFOTHR_FIFO_THR_25_40 the
          // FIFO drains in well under 500ms, so a persistent HIGH almost always means GDO2
          // is miswired / on the wrong GPIO / not connected. Warn loudly and count it so a
          // wiring fault is not silently misread as "meter asleep / out of range".
          _gdo2_stuck_timeouts++;
          echo_debug(1, "[CC1101] WARNING: GDO2 still HIGH after 500ms (TXBYTES=%u, count=%u) - check GDO2 wiring/pin or set the opt-out (DISABLE_GDO2_FIFO_MANAGEMENT / disable_gdo2_fifo_management)\n",
                     txbytes_reg & 0x7F, _gdo2_stuck_timeouts);
        }
      }
      else
      {
        // Fallback: poll TXBYTES register until <= 25 bytes remain (39 free bytes).
        // See: https://github.com/genestealer/everblu-meters-esp8266-improved/issues/58
        uint8_t wait_count = 0;
        while (wait_count < 100) // Safety limit ~500ms
        {
          uint8_t txbytes_reg = halRfReadReg(TXBYTES_ADDR);
          if (txbytes_reg & 0x80)
            break; // TXFIFO_UNDERFLOW - marcstate check at loop bottom will abort
          uint8_t num_txbytes = txbytes_reg & 0x7F;
          if (num_txbytes <= 25)
          {
            fifo_ready = true; // 64 - 25 = 39 free bytes confirmed
            break;
          }
          delay(5);
          wait_count++;
          if (wait_count % 10 == 0)
            FEED_WDT();
        }
      }
      if (fifo_ready)
      {
        SPIWriteBurstReg(TX_FIFO_ADDR, txbuffer, 39);
        if (debug_out && 0)
        {
          echo_debug(debug_out, "txbuffer:\n");
          show_in_hex_array(&txbuffer[0], 39);
        }
        wup2send = 0xFF;
      }
    }
    delay(10);
    tmo++;
    marcstate = halRfReadReg(MARCSTATE_ADDR); // read out state of cc1100 to be sure in IDLE and TX is finished this update also CC1101_status_state
    // echo_debug(debug_out,"%ifree_byte:0x%02X sts:0x%02X\n",tmo,CC1101_status_FIFO_FreeByte,CC1101_status_state);

    // MARCSTATE 0x16 means the TX FIFO has emptied. Once the full wake-up burst
    // and interrogation frame have been clocked out on-air, this is the normal,
    // expected end of transmission - there is simply no more data left to send,
    // so we stop feeding and move on to listen for the meter's reply. This is
    // NOT a code fault. (An unusually early exit here would instead indicate the
    // feeding loop fell behind the 2.4 kbps drain rate, e.g. under heavy
    // background load.)
    if ((marcstate & 0x1F) == 0x16) // TX FIFO drained - end of transmit burst
    {
      echo_debug(1, "[CC1101] Wake-up burst sent; TX FIFO drained at tmo=%d (normal end of transmit)\n", tmo);
      break;
    }
  }

  // A drained TX FIFO (MARCSTATE 0x16) is the normal end of transmit. The only
  // abnormal case here is the loop hitting its timeout WITHOUT the FIFO ever
  // draining, which points to an SPI/feeding problem rather than anything RF.
  // Reaching this point says nothing about whether the meter replied - that is
  // determined below by the ACK/data frames, so any "meter asleep / out of
  // range / run a scan" guidance is deferred until a read actually fails.
  bool tx_fifo_drained = ((marcstate & 0x1F) == 0x16);
  if (tx_fifo_drained)
  {
    echo_debug(1, "[METER] Wake-up/interrogation transmitted in %dms (MARCSTATE=0x%02X)\n", tmo * 10, marcstate & 0x1F);
  }
  else
  {
    echo_debug(1, "[METER] WARNING: TX loop timed out after %dms before the FIFO drained (MARCSTATE=0x%02X); possible SPI/feeding issue\n", tmo * 10, marcstate & 0x1F);
  }
  echo_debug(debug_out, "[CC1101] tmo=%i free_byte:0x%02X sts:0x%02X\n", tmo, CC1101_status_FIFO_FreeByte, CC1101_status_state);
  CC1101_CMD(SIDLE); // Ensure IDLE before flushing (required by CC1101 datasheet)
  CC1101_CMD(SFTX);  // Flush TX FIFO; this clears the status and puts the state machine in IDLE
  // end of transition restore default register
  halRfWriteReg(MDMCFG2, MDMCFG2_2FSK_16_16_SYNC); // Restore: 2-FSK, 16/16 sync bits
  halRfWriteReg(PKTCTRL0, PKTCTRL0_FIXED_LENGTH);  // Restore: fixed packet length

  // delay(30); //43ms de bruit
  /*34ms 0101...01  14.25ms 000...000  14ms 1111...11111  83.5ms de data acquitement*/
  echo_debug(1, "[METER] Waiting for ACK frame (18-byte frame, 150ms timeout)...\n");
  if (!receive_radian_frame(0x12, 150, rxBuffer, sizeof(rxBuffer)))
  {
    echo_debug(1, "[METER] No ACK frame received (meter may be asleep/out of range)\n");
    echo_debug(debug_out, "[METER] Meter acknowledgement frame timeout\n");
  }
  else
  {
    echo_debug(1, "[METER] ACK frame received\n");
  }
  // delay(30); //50ms de 111111  , mais on a 7+3ms de printf et xxms calculs
  /*34ms 0101...01  14.25ms 000...000  14ms 1111...11111  582ms de data avec l'index */
  echo_debug(1, "[METER] Waiting for data frame (124-byte frame, 1000ms timeout)...\n");
  rxBuffer_size = receive_radian_frame(0x7C, 1000, rxBuffer, sizeof(rxBuffer));
  if (rxBuffer_size)
  {
    echo_debug(1, "[METER] Data frame received - decoding %d raw bytes...\n", rxBuffer_size);
    if (debug_out)
    {
      // Raw pre-decode oversampled buffer, for offline analysis of the full
      // frame (captured to end of transmission, not a fixed 124-byte window).
      echo_debug(debug_out, "[CC1101] Raw pre-decode RX buffer (%d oversampled bytes):\n", rxBuffer_size);
      show_in_hex_array(rxBuffer, rxBuffer_size);
    }

    meter_data_size = decode_4bitpbit_serial(rxBuffer, rxBuffer_size, meter_data);
    // If debug enabled, print the decoded (post-serial-decoding) meter data so we can inspect fields (timestamp etc.)
    echo_debug(1, "[METER] Decoded %d bytes from %d raw bytes\n", meter_data_size, rxBuffer_size);

    // Always show hex dump when debug_cc1101 is enabled for field-level debugging
    if (debug_out)
    {
      echo_debug(debug_out, "[CC1101] Full hex dump of decoded frame (%d bytes):\n", meter_data_size);
      echo_debug(debug_out, "Offset  : Hex Data\n");
      // Show in 16-byte rows with offset markers for easier analysis
      for (int i = 0; i < meter_data_size; i += 16)
      {
        char hex_line[128];
        int line_pos = 0;
        int end_offset = (i + 15 < meter_data_size - 1) ? i + 15 : meter_data_size - 1;
        line_pos += snprintf(hex_line + line_pos, sizeof(hex_line) - line_pos, "[%03d-%03d]: ", i, end_offset);
        int max_j = (i + 16 < meter_data_size) ? i + 16 : meter_data_size;
        for (int j = i; j < max_j; j++)
        {
          line_pos += snprintf(hex_line + line_pos, sizeof(hex_line) - line_pos, "%02X ", meter_data[j]);
        }
        echo_debug(debug_out, "%s\n", hex_line);
      }
      echo_debug(debug_out, "Note: Bytes [18-21]=volume, [31]=battery, [44-45]=wake/sleep, [66-117]=history\n");
    }
    else
    {
      // Even without debug_cc1101, show first 32 bytes for basic troubleshooting
      echo_debug(1, "[METER] First 32 bytes (header + volume field): ");
      show_in_hex_one_line(meter_data, (meter_data_size < 32) ? meter_data_size : 32);
    }

    // Diagnostic: find where a valid CRC actually closes rather than trusting
    // the 0x7C length byte. Reveals the true frame length + CRC convention.
    if (debug_out)
      crc_boundary_search(meter_data, meter_data_size);

    echo_debug(1, "[METER] Validating CRC...\n");
    // Read RSSI now while the channel is still active so we can use it to
    // diagnose the cause of a CRC failure (saturation vs. weak signal).
    int8_t frame_rssi_dbm = cc1100_rssi_convert2dbm(halRfReadReg(RSSI_ADDR));
    if (validate_radian_crc(meter_data, meter_data_size))
    {
      echo_debug(1, "[METER] CRC valid - parsing meter data\n");
      sdata = parse_meter_report(meter_data, meter_data_size);
    }
    else
    {
      echo_debug(1, "[METER] CRC check failed: a frame was received but arrived corrupted, so this reading was discarded.\n");
      echo_debug(1, "[METER] (diag) decoded %d bytes; see the [CRC-SCAN] lines above (enable debug_cc1101) for the true CRC boundary/length.\n", meter_data_size);
      if (frame_rssi_dbm > -50)
      {
        // Very strong signal: near-field RF saturation is the likely cause.
        // The CC1101 front-end clips when the input exceeds its linear range,
        // producing frames that look valid (correct header bytes) but fail CRC.
        // This is the OPPOSITE of a weak-signal problem.
        echo_debug(1, "[METER] *** NEAR-FIELD SATURATION DETECTED (RSSI=%d dBm) ***\n", frame_rssi_dbm);
        echo_debug(1, "[METER] The signal is too STRONG, not too weak. Move the device at least 1-2 m away from the meter.\n");
        echo_debug(1, "[METER] Set #define RX_ATTENUATION_DB 6 (or 12/18) in private.h to engage the CC1101 front-end LNA gain limiter.\n");
      }
      else
      {
        echo_debug(1, "[METER] This points to a marginal/noisy RF link (weak signal or a slight frequency offset), not a code fault. Improving antenna placement or running a frequency scan usually fixes it.\n");
      }
      meter_data_size = 0;
    }
  }
  else
  {
    echo_debug(1, "[METER] No data frame received within the timeout window - the meter did not respond.\n");
    echo_debug(1, "[METER] This usually means the meter is asleep (outside its daily listening window), out of range, the signal is too weak, or the configured Year/Serial is incorrect.\n");
    echo_debug(1, "[METER] If this persists, try improving antenna placement or running a frequency scan to recalibrate the radio.\n");
    echo_debug(1, "[METER] A scan runs automatically after repeated failures unless disabled (AUTO_SCAN_ON_FAILURE_ENABLED / auto_scan_on_failure); for a full re-scan see AUTO_SCAN_ENABLED / CLEAR_EEPROM_ON_BOOT.\n");
    echo_debug(debug_out, "[METER] Meter data frame timeout\n");
  }
  sdata.rssi = halRfReadReg(RSSI_ADDR);                              // Read RSSI value from CC1101
  sdata.rssi_dbm = cc1100_rssi_convert2dbm(halRfReadReg(RSSI_ADDR)); // Read RSSI value from CC1101 and convert to dBm
  sdata.lqi = halRfReadReg(LQI_ADDR) & 0x7F;                         // Read LQI value from CC1101 (mask bit 7 = CRC_OK; bits 6:0 are the LQI)
  sdata.freqest = (int8_t)halRfReadReg(FREQEST_ADDR);                // Read frequency offset estimate for adaptive tracking
  return sdata;
}

struct tmeter_data get_meter_data(void)
{
#if defined(USE_ESPHOME)
  // ESPHome multi-instance flows should use get_meter_data_for_meter().
  // Fail fast to avoid a blocking radio transaction with an invalid identity.
  echo_debug(1, "[METER] get_meter_data() is unsupported in USE_ESPHOME; use get_meter_data_for_meter()\n");
  struct tmeter_data sdata = {};
  return sdata;
#else
  struct tmeter_data sdata = {};

#if !defined(METER_CODE)
  echo_debug(1, "[METER] METER_CODE is not defined; cannot read meter\n");
  return sdata;
#else
  const char *meter_code = METER_CODE;
  uint8_t meter_year = 0;
  uint32_t meter_serial = 0;
  if (!everblu::core::parseMeterCode(meter_code, &meter_year, &meter_serial))
  {
    echo_debug(1, "[METER] Invalid METER_CODE: expected YY-SSSSSSS or YY-SSSSSSS-NNN\n");
    return sdata;
  }

  return get_meter_data_for_meter(meter_year, meter_serial);
#endif
#endif
}
