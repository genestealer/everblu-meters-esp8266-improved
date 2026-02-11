/*  CC1101 radio interface for Itron EverBlu Cyble Enhanced water meters */
/*  Implements RADIAN protocol communication over 433 MHz RF */

#include "utils.h"   // Utility functions
#include "cc1101.h"  // CC1101 interface
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
#include <SPI.h>         // Include the SPI library for SPI communication
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
#define IOCFG2_SERIAL_DATA_OUTPUT 0x0D // GDO2: Serial Data Output

// IOCFG0 - GDO0 Output Pin Configuration
#define IOCFG0_SYNC_WORD_DETECT 0x06 // Asserts when sync word detected, deasserts at end of packet

// FIFOTHR - RX FIFO and TX FIFO Thresholds
#define FIFOTHR_FIFO_THR_33_32 0x47 // RX FIFO threshold: 33 bytes, TX FIFO threshold: 32 bytes

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
#define MDMCFG4_RX_BW_58KHZ 0xF6         // RX filter bandwidth = 58 kHz, 2.4 kbps
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
#define FOCCFG_FOC_4K_2K 0x1D // FOC enabled, 4K before sync, K/2 after sync

// BSCFG - Bit Synchronization Configuration
#define BSCFG_BS_PRE_KI_2 0x1C // Bit sync configuration

// AGCCTRL2 - AGC Control
#define AGCCTRL2_MAX_DVGA_LNA 0xC7 // Max DVGA and LNA gain

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
// ESPHome SPI integration - store SPIDevice instance and CS pin
// The EverbluMeterComponent inherits from this SPIDevice specialization.
using CC1101SpiDevice = esphome::spi::SPIDevice<esphome::spi::BIT_ORDER_MSB_FIRST,
                                                esphome::spi::CLOCK_POLARITY_LOW,
                                                esphome::spi::CLOCK_PHASE_LEADING,
                                                esphome::spi::DATA_RATE_1MHZ>;
static CC1101SpiDevice *_spi_device = nullptr;
static int _spi_cs_pin = -1;
static int _gdo0_pin = -1;

void cc1101_set_spi_device(void *device, int cs_pin)
{
  // Store the SPIDevice pointer for ESPHome SPI transactions
  _spi_device = static_cast<CC1101SpiDevice *>(device);
  _spi_cs_pin = cs_pin;
}

void cc1101_set_gdo0_pin(int gdo0_pin)
{
  _gdo0_pin = gdo0_pin;
}

// Macro to get GDO0 pin - use variable in ESPHome mode, build flag otherwise
#define GET_GDO0_PIN() (_gdo0_pin)
#else
// Non-ESPHome mode: GDO0 comes from build flag
#define GET_GDO0_PIN() (GDO0)
#endif

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
  // ESPHome mode: SPI is already configured by SPIDevice
  _spi_speed = speed; // Store for potential future use
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
  halRfWriteReg(IOCFG2, IOCFG2_SERIAL_DATA_OUTPUT); // GDO2: Serial data output
  halRfWriteReg(IOCFG0, IOCFG0_SYNC_WORD_DETECT);   // GDO0: Sync word detection
  halRfWriteReg(FIFOTHR, FIFOTHR_FIFO_THR_33_32);   // FIFO thresholds
  halRfWriteReg(SYNC1, SYNC1_PATTERN_55);           // Sync word MSB: 0x55
  halRfWriteReg(SYNC0, SYNC0_PATTERN_00);           // Sync word LSB: 0x00

  halRfWriteReg(PKTCTRL1, PKTCTRL1_NO_ADDR_CHECK); // No address check
  halRfWriteReg(PKTCTRL0, PKTCTRL0_FIXED_LENGTH);  // Fixed packet length
  halRfWriteReg(FSCTRL1, FSCTRL1_FREQ_IF);         // Frequency synthesizer IF

  setMHZ(freq); // Configure frequency using helper function

  halRfWriteReg(MDMCFG4, MDMCFG4_RX_BW_58KHZ);         // RX bandwidth: 58 kHz
  halRfWriteReg(MDMCFG3, MDMCFG3_DRATE_2_4KBPS);       // Data rate: 2.4 kbps
  halRfWriteReg(MDMCFG2, MDMCFG2_2FSK_16_16_SYNC);     // 2-FSK, 16/16 sync bits
  halRfWriteReg(MDMCFG1, MDMCFG1_NUM_PREAMBLE_2);      // Preamble: 2 bytes
  halRfWriteReg(MDMCFG0, MDMCFG0_CHANSPC_25KHZ);       // Channel spacing: 25 kHz
  halRfWriteReg(DEVIATN, DEVIATN_5_157KHZ);            // Deviation: 5.157 kHz
  halRfWriteReg(MCSM1, MCSM1_CCA_ALWAYS_IDLE);         // CCA always, idle on exit
  halRfWriteReg(MCSM0, MCSM0_FS_AUTOCAL_IDLE_TO_RXTX); // Auto-calibrate on IDLE→RX/TX
  halRfWriteReg(FOCCFG, FOCCFG_FOC_4K_2K);             // Frequency offset compensation
  halRfWriteReg(BSCFG, BSCFG_BS_PRE_KI_2);             // Bit synchronization
  halRfWriteReg(AGCCTRL2, AGCCTRL2_MAX_DVGA_LNA);      // AGC: max gain
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
  pinMode(GET_GDO0_PIN(), INPUT_PULLUP);

  // Initialize SPI bus for CC1101 communication (500 kHz)
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
  CC1101_CMD(SIDLE);                                                        // sets to idle first. must be in
  CC1101_CMD(SRX);                                                          // writes receive strobe (receive mode)
  marcstate = 0xFF;                                                         // set unknown/dummy state value
  while ((marcstate != 0x0D) && (marcstate != 0x0E) && (marcstate != 0x0F)) // 0x0D = RX
  {
    marcstate = halRfReadReg(MARCSTATE_ADDR); // read out state of cc1100 to be sure in RX
    FEED_WDT();                               // Avoid soft WDT while waiting for RX state
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
  int i, ret;
  ret = FALSE;
  for (i = 0; i < len; i++)
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
    // get RF info at beginning of the frame
    l_lqi = halRfReadReg(LQI_ADDR);
    l_freq_est = halRfReadReg(FREQEST_ADDR);
    l_Rssi_dbm = cc1100_rssi_convert2dbm(halRfReadReg(RSSI_ADDR));

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
        break;
      }
    }
    if (is_look_like_radian_frame(rxBuffer, pktLen))
    {
      echo_debug(debug_out, "[CC1101] Packet looks like RADIAN frame");
      echo_debug(debug_out, "[CC1101] bytes=%u rssi=%d lqi=%u F_est=%u", pktLen, l_Rssi_dbm, l_lqi, l_freq_est);
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

static bool validate_radian_crc(const uint8_t *decoded_buffer, size_t size)
{
  if (size < 4)
  {
    echo_debug(1, "[ERROR] Decoded frame too small for CRC validation (size=%u)\n", size);
    return false;
  }

  const uint8_t length_field = decoded_buffer[0];
  size_t expected_len = length_field ? length_field : size;

  if (expected_len > size)
  {
    const size_t missing = expected_len - size;
    // Many EverBlu meters advertise 0x7C (124) bytes but actually deliver 122 bytes,
    // meaning the CRC bytes are not present in the decoded payload. Log the situation
    // but keep the frame so we don't regress existing setups.
    echo_debug(debug_out,
               "[WARN] RADIAN frame missing %u byte(s) from advertised length (expected=%u got=%u)\n",
               (unsigned)missing, (unsigned)expected_len, (unsigned)size);
    if (missing == 2)
    {
      echo_debug(debug_out, "[WARN] CRC bytes absent in payload - skipping CRC validation\n");
      return true;
    }
    // For any other mismatch we can't meaningfully validate, so accept the frame but warn.
    echo_debug(debug_out, "[WARN] Length mismatch prevents CRC validation - accepting frame\n");
    return true;
  }

  if (expected_len < 4)
  {
    echo_debug(1, "[ERROR] Invalid RADIAN length byte (%u)\n", length_field);
    return false;
  }

  if (expected_len < size)
  {
    echo_debug(debug_out,
               "[WARN] Decoder produced %u bytes but length byte indicates %u - extra tail ignored for CRC\n",
               (unsigned)size, (unsigned)expected_len);
  }

  const size_t crc_offset = expected_len - 2;
  if (crc_offset + 1 >= size)
  {
    echo_debug(debug_out, "[WARN] Not enough data to read CRC bytes - accepting frame\n");
    return true;
  }

  const uint16_t received_crc = ((uint16_t)decoded_buffer[crc_offset] << 8) |
                                (uint16_t)decoded_buffer[crc_offset + 1];

  // RADIAN/wM-Bus frames place the length byte ahead of the CRC-protected
  // fields. The CRC covers C + addresses + payload, i.e. everything after the
  // length byte up to (but not including) the CRC itself. Excluding the length
  // byte fixes the persistent mismatches observed in the field.
  if (expected_len <= 3)
  {
    echo_debug(1, "[ERROR] RADIAN frame too short for CRC validation (len=%u)\n", (unsigned)expected_len);
    return false;
  }

  const uint16_t computed_crc = crc_kermit(&decoded_buffer[1], expected_len - 3);

  if (computed_crc != received_crc)
  {
    echo_debug(1, "[ERROR] RADIAN CRC mismatch (computed=0x%04X frame=0x%04X) - discarding frame\n",
               computed_crc, received_crc);
    return false;
  }

  return true;
}

struct tmeter_data parse_meter_report(uint8_t *decoded_buffer, uint8_t size)
{
  struct tmeter_data data;
  memset(&data, 0, sizeof(data)); // Initialize all fields to zero

  // Bounds check: ensure buffer is large enough for basic data
  if (size < 30)
  {
    echo_debug(1, "[ERROR] Buffer too small for meter data (size=%d, need>=30)\n", size);
    return data;
  }

  // Extract volume using proper uint32_t handling to prevent overflow
  // Byte order is LSB first: [18]=LSB, [19], [20], [21]=MSB
  data.volume = ((uint32_t)decoded_buffer[18]) |
                ((uint32_t)decoded_buffer[19] << 8) |
                ((uint32_t)decoded_buffer[20] << 16) |
                ((uint32_t)decoded_buffer[21] << 24);

  // Basic plausibility checks for primary fields. These are deliberately
  // conservative so we only reject clearly bogus frames while keeping the
  // behaviour compatible with good data.
  if (data.volume == 0 || data.volume == 0xFFFFFFFFUL)
  {
    echo_debug(1, "[ERROR] Parsed volume value is invalid (0x%08lX) - discarding frame\n",
               (unsigned long)data.volume);
    memset(&data, 0, sizeof(data));
    return data;
  }

  // Extract extended data if buffer is large enough
  if (size >= 49)
  { // Need at least 49 bytes to safely access decoded_buffer[48]
    // echo_debug(1,"Num %u %u Mois %uh-%uh ",decoded_buffer[48], decoded_buffer[31],decoded_buffer[44],decoded_buffer[45]);
    data.reads_counter = decoded_buffer[48];
    data.battery_left = decoded_buffer[31];
    data.time_start = decoded_buffer[44];
    data.time_end = decoded_buffer[45];

    // Sanity checks on extended fields: time window must be within 0-23,
    // and battery_left should not be 0xFF (usually reserved / invalid).
    if (data.time_start > 23 || data.time_end > 23)
    {
      echo_debug(1, "[ERROR] Invalid wake window %u-%u (expected 0-23) - discarding frame\n",
                 data.time_start, data.time_end);
      memset(&data, 0, sizeof(data));
      return data;
    }

    if (data.battery_left == 0xFF)
    {
      echo_debug(1, "[ERROR] Invalid battery_left value 0xFF - discarding frame\n");
      memset(&data, 0, sizeof(data));
      return data;
    }

    if (data.reads_counter == 0xFF)
    {
      echo_debug(1, "[ERROR] Invalid reads_counter value 0xFF (255) - discarding frame\n");
      memset(&data, 0, sizeof(data));
      return data;
    }
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
        if (newest > data.volume)
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
// Note: decoded_buffer must be at least l_total_byte/4 bytes in size
uint8_t decode_4bitpbit_serial(uint8_t *rxBuffer, int l_total_byte, uint8_t *decoded_buffer)
{
  uint16_t i, j, k;
  uint8_t bit_cnt = 0;
  int8_t bit_cnt_flush_S8 = 0;
  uint8_t bit_pol = 0;
  uint8_t dest_bit_cnt = 0;
  uint8_t dest_byte_cnt = 0;
  uint8_t current_Rx_Byte;

  // Maximum decoded buffer size (conservative estimate: input bytes / 4)
  const uint8_t MAX_DECODED_SIZE = 200;
  // Track framing/stop-bit errors so we can reject extremely corrupted frames.
  uint8_t framing_error_count = 0;

  // show_in_hex(rxBuffer,l_total_byte);
  /*set 1st bit polarity*/
  bit_pol = (rxBuffer[0] & 0x80); // initialize with 1st bit state

  for (i = 0; i < l_total_byte; i++)
  {
    current_Rx_Byte = rxBuffer[i];
    // echo_debug(debug_out, "0x%02X ", rxBuffer[i]);

    // Feed watchdog periodically during long decode operations
    if (i > 0 && (i % 64) == 0)
    {
      FEED_WDT();
    }

    for (j = 0; j < 8; j++)
    {
      if ((current_Rx_Byte & 0x80) == bit_pol)
        bit_cnt++;
      else if (bit_cnt == 1)
      {                                   // previous bit was a glitch so bit has not really changed
        bit_pol = current_Rx_Byte & 0x80; // restore correct bit polarity
        bit_cnt = bit_cnt_flush_S8 + 1;   // hope that previous bit was correctly decoded
      }
      else
      { // bit polarity has changed
        bit_cnt_flush_S8 = bit_cnt;
        bit_cnt = (bit_cnt + 2) / 4;
        bit_cnt_flush_S8 = bit_cnt_flush_S8 - (bit_cnt * 4);

        for (k = 0; k < bit_cnt; k++)
        { // insert the number of decoded bits
          if (dest_bit_cnt < 8)
          { // if data byte
            // Bounds check before writing to buffer
            if (dest_byte_cnt >= MAX_DECODED_SIZE)
            {
              echo_debug(debug_out, "[ERROR] Decode buffer overflow at byte %d\n", dest_byte_cnt);
              return dest_byte_cnt;
            }
            decoded_buffer[dest_byte_cnt] = decoded_buffer[dest_byte_cnt] >> 1;
            decoded_buffer[dest_byte_cnt] |= bit_pol;
          }
          dest_bit_cnt++;
          // if ((dest_bit_cnt ==9) && (!bit_pol)){  echo_debug(debug_out,"stop bit error9"); return dest_byte_cnt;}
          if ((dest_bit_cnt == 10) && (!bit_pol))
          {
            // A stop-bit mismatch was detected. Instead of aborting the whole
            // decode (which produces tiny decoded outputs and breaks parsing),
            // log the error, skip the current malformed byte and continue
            // decoding. This makes the decoder more tolerant to single-bit
            // corruption or brief polarity glitches on the air.
            echo_debug(debug_out, "[ERROR] Stop bit error at bit 10 - skipping malformed byte\n");
            if (framing_error_count < 255)
            {
              framing_error_count++;
            }
            // Reset the bit counter to align on the next byte boundary and
            // advance to the next destination byte slot so we don't overwrite
            // the current (corrupted) byte.
            dest_bit_cnt = 0;
            dest_byte_cnt++;
            if (dest_byte_cnt >= MAX_DECODED_SIZE)
            {
              echo_debug(debug_out, "[ERROR] Decode buffer size limit reached while skipping malformed byte\n");
              return dest_byte_cnt;
            }
            // Continue processing remaining samples
            continue;
          }
          if ((dest_bit_cnt >= 11) && (!bit_pol)) // start bit
          {
            dest_bit_cnt = 0;
            // echo_debug(debug_out, " dec[%i]=0x%02X \n", dest_byte_cnt, decoded_buffer[dest_byte_cnt]);
            dest_byte_cnt++;
            // Additional bounds check before next iteration
            if (dest_byte_cnt >= MAX_DECODED_SIZE)
            {
              echo_debug(debug_out, "[ERROR] Decode buffer size limit reached\n");
              return dest_byte_cnt;
            }
          }
        }
        bit_pol = current_Rx_Byte & 0x80;
        bit_cnt = 1;
      }
      current_Rx_Byte = current_Rx_Byte << 1;
    } // scan TX_bit
  } // scan TX_byte

  // If we saw many framing errors compared to the number of bytes we managed
  // to decode, treat the whole frame as unusable. This prevents obviously
  // corrupted frames from being interpreted as valid meter data.
  if (dest_byte_cnt > 0 && framing_error_count > (dest_byte_cnt / 2))
  {
    echo_debug(debug_out,
               "[ERROR] Decode quality too low (decoded=%u, framing_errors=%u) - discarding frame\n",
               dest_byte_cnt, framing_error_count);
    return 0;
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
  uint16_t l_radian_frame_size_byte = ((size_byte * (8 + 3)) / 8) + 1;
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
    echo_debug(debug_out, "[ERROR] Timeout waiting for GDO0 (sync detection)\n");
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
    echo_debug(debug_out, "[ERROR] Timeout waiting for sync pattern bytes\n");
    return 0;
  }

  l_lqi = halRfReadReg(LQI_ADDR);
  l_freq_est = halRfReadReg(FREQEST_ADDR);
  l_Rssi_dbm = cc1100_rssi_convert2dbm(halRfReadReg(RSSI_ADDR));
  echo_debug(debug_out, "[CC1101] rssi=%d lqi=%u F_est=%u", l_Rssi_dbm, l_lqi, l_freq_est);

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
  uint16_t l_expected_bytes = l_radian_frame_size_byte * 4;
  while ((l_total_byte < l_expected_bytes) && (l_tmo < rx_tmo_ms))
  {
    delay(5);
    l_tmo += 5; // wait for some byte received
    if (l_tmo % 50 == 0)
      FEED_WDT(); // Feed watchdog every 50ms during frame receive
    l_byte_in_rx = (halRfReadReg(RXBYTES_ADDR) & RXBYTES_MASK);
    if (l_byte_in_rx)
    {
      // Do not pull more than we expect; excess bytes are noise and will
      // skew CRC. Clamp the burst to the remaining expected length.
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

struct tmeter_data get_meter_data(void)
{
  // Avoid leading newline so ESPHome log doesn't emit an empty line first
  echo_debug(1, "[METER] Starting meter read sequence...\n");
  struct tmeter_data sdata;
  uint8_t marcstate = 0xFF;
  uint8_t wupbuffer[] = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
  uint8_t wup2send = 77;
  uint16_t tmo = 0;
  static uint8_t rxBuffer[1000]; // Make static to avoid stack overflow
  int rxBuffer_size;
  static uint8_t meter_data[200]; // Make static to avoid stack overflow
  uint8_t meter_data_size = 0;

  memset(&sdata, 0, sizeof(sdata));
  memset(rxBuffer, 0, sizeof(rxBuffer));     // Clear static buffer
  memset(meter_data, 0, sizeof(meter_data)); // Clear static buffer

  uint8_t txbuffer[100];
  Make_Radian_Master_req(txbuffer, METER_YEAR, METER_SERIAL);

  echo_debug(1, "[METER] Transmitting wake-up + interrogation (Year=%d, Serial=%lu)...\n",
             METER_YEAR, (unsigned long)METER_SERIAL);
  halRfWriteReg(MDMCFG2, MDMCFG2_NO_PREAMBLE_SYNC);  // No preamble/sync for WUP
  halRfWriteReg(PKTCTRL0, PKTCTRL0_INFINITE_LENGTH); // Infinite packet length
  SPIWriteBurstReg(TX_FIFO_ADDR, wupbuffer, 8);
  wup2send--;
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
    else
    {
      delay(130); // 130ms time to free 39bytes FIFO space
      SPIWriteBurstReg(TX_FIFO_ADDR, txbuffer, 39);
      if (debug_out && 0)
      {
        echo_debug(debug_out, "txbuffer:\n");
        show_in_hex_array(&txbuffer[0], 39);
      }
      wup2send = 0xFF;
    }
    delay(10);
    tmo++;
    marcstate = halRfReadReg(MARCSTATE_ADDR); // read out state of cc1100 to be sure in IDLE and TX is finished this update also CC1101_status_state
    // echo_debug(debug_out,"%ifree_byte:0x%02X sts:0x%02X\n",tmo,CC1101_status_FIFO_FreeByte,CC1101_status_state);
  }
  echo_debug(1, "[METER] TX complete after %dms (MARCSTATE=0x%02X)\n", tmo * 10, marcstate & 0x1F);
  echo_debug(debug_out, "[CC1101] tmo=%i free_byte:0x%02X sts:0x%02X", tmo, CC1101_status_FIFO_FreeByte, CC1101_status_state);
  CC1101_CMD(SFTX); // flush the Tx_fifo content this clear the status state and put sate machin in IDLE
  // end of transition restore default register
  halRfWriteReg(MDMCFG2, MDMCFG2_2FSK_16_16_SYNC); // Restore: 2-FSK, 16/16 sync bits
  halRfWriteReg(PKTCTRL0, PKTCTRL0_FIXED_LENGTH);  // Restore: fixed packet length

  // delay(30); //43ms de bruit
  /*34ms 0101...01  14.25ms 000...000  14ms 1111...11111  83.5ms de data acquitement*/
  echo_debug(1, "[METER] Waiting for ACK frame (18-byte frame, 150ms timeout)...\n");
  if (!receive_radian_frame(0x12, 150, rxBuffer, sizeof(rxBuffer)))
  {
    echo_debug(1, "[METER] No ACK frame received (meter may be asleep/out of range)\n");
    echo_debug(debug_out, "[ERROR] Timeout waiting for meter acknowledgement frame\n");
  }
  else
  {
    echo_debug(1, "[METER] ACK frame received\n");
  }
  // delay(30); //50ms de 111111  , mais on a 7+3ms de printf et xxms calculs
  /*34ms 0101...01  14.25ms 000...000  14ms 1111...11111  582ms de data avec l'index */
  echo_debug(1, "[METER] Waiting for data frame (124-byte frame, 700ms timeout)...\n");
  rxBuffer_size = receive_radian_frame(0x7C, 700, rxBuffer, sizeof(rxBuffer)); // 700ms timeout matches working 91jj ESPHome implementation
  if (rxBuffer_size)
  {
    echo_debug(1, "[METER] Data frame received - decoding %d raw bytes...\n", rxBuffer_size);
    if (debug_out)
    {
      //  echo_debug(debug_out, "rxBuffer:\n");
      //  show_in_hex_array(rxBuffer, rxBuffer_size);
    }

    meter_data_size = decode_4bitpbit_serial(rxBuffer, rxBuffer_size, meter_data);
    // show_in_hex(meter_data,meter_data_size);
    // If debug enabled, print the decoded (post-serial-decoding) meter data so we can inspect fields (timestamp etc.)
    echo_debug(1, "[METER] Decoded %d bytes from %d raw bytes\n", meter_data_size, rxBuffer_size);
    if (debug_out)
    {
      echo_debug(debug_out, "[CC1101] Decoded meter data size = %d\n", meter_data_size);
      show_in_hex_one_line(meter_data, meter_data_size);
    }

    echo_debug(1, "[METER] Validating CRC...\n");
    if (validate_radian_crc(meter_data, meter_data_size))
    {
      echo_debug(1, "[METER] CRC valid - parsing meter data\n");
      sdata = parse_meter_report(meter_data, meter_data_size);
    }
    else
    {
      echo_debug(1, "[METER] ERROR: CRC validation failed\n");
      meter_data_size = 0;
    }
  }
  else
  {
    echo_debug(1, "[METER] ERROR: No data frame received (timeout)\n");
    echo_debug(debug_out, "[ERROR] Timeout waiting for meter data frame\n");
  }
  sdata.rssi = halRfReadReg(RSSI_ADDR);                              // Read RSSI value from CC1101
  sdata.rssi_dbm = cc1100_rssi_convert2dbm(halRfReadReg(RSSI_ADDR)); // Read RSSI value from CC1101 and convert to dBm
  sdata.lqi = halRfReadReg(LQI_ADDR);                                // Read LQI value from CC1101
  sdata.freqest = (int8_t)halRfReadReg(FREQEST_ADDR);                // Read frequency offset estimate for adaptive tracking
  return sdata;
}
