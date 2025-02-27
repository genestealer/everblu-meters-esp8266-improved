/*  the radian_trx SW shall not be distributed  nor used for commercial product*/
/*  it is exposed just to demonstrate CC1101 capability to reader water meter indexes */
/*  there is no Warranty on radian_trx SW */

#include <arduino.h>
#include <SPI.h>
#include "everblu_meters.h"
#include <private.h> // Passwords etc. not for GitHub
#include "utils.h"
#include "cc1101.h"

uint8_t RF_config_u8 = 0xFF;
uint8_t RF_Test_u8 = 0;
//                     +10,  +7,   5,   0, -10, -15, -20, -30
uint8_t PA_Test[] = { 0xC0,0xC8,0x85,0x60,0x34,0x1D,0x0E,0x12, };
uint8_t PA[] = { 0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00, };
uint8_t CC1101_status_state = 0;
uint8_t CC1101_status_FIFO_FreeByte = 0;
uint8_t CC1101_status_FIFO_ReadByte = 0;
uint8_t debug_out = 0;

#ifndef TRUE
#define TRUE true
#endif

#ifndef FALSE
#define FALSE true
#endif

#define TX_LOOP_OUT 300
/*---------------------------[CC1100 - R/W offsets]------------------------------*/
#define WRITE_SINGLE_BYTE  		0x00
#define WRITE_BURST  			0x40
#define READ_SINGLE_BYTE  		0x80
#define READ_BURST  			0xC0

/*-------------------------[CC1100 - config register]----------------------------*/
#define IOCFG2  			0x00                                    // GDO2 output pin configuration
#define IOCFG1  			0x01                                    // GDO1 output pin configuration
#define IOCFG0  			0x02                                    // GDO0 output pin configuration
#define FIFOTHR  			0x03                                    // RX FIFO and TX FIFO thresholds
#define SYNC1  			    0x04                                    // Sync word, high byte
#define SYNC0  			    0x05                                    // Sync word, low byte
#define PKTLEN  			0x06                                    // Packet length
#define PKTCTRL1 			0x07                                  	// Packet automation control
#define PKTCTRL0  			0x08                                  	// Packet automation control
#define ADDRR  				0x09                                    // Device address
#define CHANNR  			0x0A                                    // Channel number
#define FSCTRL1  			0x0B                                   	// Frequency synthesizer control
#define FSCTRL0  			0x0C                                   	// Frequency synthesizer control
#define FREQ2  			    0x0D                                    // Frequency control word, high byte
#define FREQ1  			    0x0E                                    // Frequency control word, middle byte
#define FREQ0  			    0x0F                                    // Frequency control word, low byte

#define MDMCFG4  			0x10                                   	// Modem configuration
#define MDMCFG3  			0x11                                   	// Modem configuration
#define MDMCFG2  			0x12                                   	// Modem configuration
#define MDMCFG1  			0x13                                   	// Modem configuration
#define MDMCFG0  			0x14                                   	// Modem configuration
#define DEVIATN  			0x15                                   	// Modem deviation setting
#define MCSM2  			0x16                                    // Main Radio Cntrl State Machine config
#define MCSM1  			0x17                                    // Main Radio Cntrl State Machine config
#define MCSM0  			0x18                                    // Main Radio Cntrl State Machine config
#define FOCCFG  			0x19	                                // Frequency Offset Compensation config
#define BSCFG  			0x1A                                    // Bit Synchronization configuration
#define AGCCTRL2 			0x1B                                    // AGC control
#define AGCCTRL1 			0x1C                                    // AGC control
#define AGCCTRL0 			0x1D                                    // AGC control
#define WOREVT1 			0x1E                                   	// High byte Event 0 timeout
#define WOREVT0 			0x1F                                   	// Low byte Event 0 timeout

#define WORCTRL 			0x20                                   	// Wake On Radio control
#define FREND1 			0x21                                    // Front end RX configuration
#define FREND0 			0x22                                    // Front end TX configuration
#define FSCAL3 			0x23                                    // Frequency synthesizer calibration
#define FSCAL2 			0x24                                    // Frequency synthesizer calibration
#define FSCAL1 			0x25                                    // Frequency synthesizer calibration
#define FSCAL0 			0x26                                    // Frequency synthesizer calibration
#define RCCTRL1 			0x27                                   	// RC oscillator configuration
#define RCCTRL0 			0x28                                   	// RC oscillator configuration
#define FSTEST 			0x29                                   	// Frequency synthesizer cal control
#define PTEST 				0x2A                                    // Production test
#define AGCTEST 			0x2B                                   	// AGC test
#define TEST2 				0x2C                                    // Various test settings
#define TEST1 				0x2D                                    // Various test settings
#define TEST0 				0x2E                                    // Various test settings

// Change these define according to your ESP8266 board
#ifdef ESP8266
#define SPI_CSK  PIN_SPI_SCK
#define SPI_MISO PIN_SPI_MISO
#define SPI_MOSI PIN_SPI_MOSI
#define SPI_SS   PIN_SPI_SS
#endif

// Change these define according to your ESP32 board
#ifdef ESP32
#define SPI_CSK  SCK
#define SPI_MISO MISO
#define SPI_MOSI MOSI
#define SPI_SS   SS
#endif

int _spi_speed = 0;
int wiringPiSPIDataRW(int channel, unsigned char *data, int len)
{
  if (!_spi_speed) return -1;

  SPI.beginTransaction(SPISettings(_spi_speed, MSBFIRST, SPI_MODE0));
  digitalWrite(SPI_SS, 0);

  //echo_debug(debug_out, "wiringPiSPIDataRW(0x%02X, %d)\n", (len > 0) ? data[0] : 'X' , len);

  SPI.transfer(data, len);

  digitalWrite(SPI_SS, 1);
  SPI.endTransaction();

  return 0;
}

int wiringPiSPISetup(int channel, int speed)
{
  _spi_speed = speed;

  pinMode(SPI_SS, OUTPUT);
  digitalWrite(SPI_SS, 1);

#ifdef ESP8266
  SPI.pins(SPI_CSK, SPI_MISO, SPI_MOSI, SPI_SS);
  SPI.begin();
#endif

#ifdef ESP32
  SPI.begin(SPI_CSK, SPI_MISO, SPI_MOSI, SPI_SS);
#endif

  return 0;
}

/*----------------------------[END config register]------------------------------*/
//------------------[write register]--------------------------------
uint8_t halRfWriteReg(uint8_t reg_addr, uint8_t value)
{
  uint8_t tbuf[2] = { 0 };
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
#define PARTNUM_ADDR 			0xF0				// Part number
#define VERSION_ADDR 			0xF1				// Current version number
#define FREQEST_ADDR 			0xF2				// Frequency offset estimate
#define LQI_ADDR 				0xF3				// Demodulator estimate for link quality
#define RSSI_ADDR 				0xF4				// Received signal strength indication
#define MARCSTATE_ADDR 			0xF5				// Control state machine state
#define WORTIME1_ADDR 			0xF6				// High byte of WOR timer
#define WORTIME0_ADDR 			0xF7				// Low byte of WOR timer
#define PKTSTATUS_ADDR 			0xF8				// Current GDOx status and packet status
#define VCO_VC_DAC_ADDR 		0xF9				// Current setting from PLL cal module
#define TXBYTES_ADDR 			0xFA				// Underflow and # of bytes in TXFIFO
#define RXBYTES_ADDR 			0xFB				// Overflow and # of bytes in RXFIFO
//----------------------------[END status register]-------------------------------
#define RXBYTES_MASK            0x7F        // Mask "# of bytes" field in _RXBYTES

uint8_t halRfReadReg(uint8_t spi_instr)
{
  uint8_t value;
  uint8_t rbuf[2] = { 0 };
  uint8_t len = 2;

  //rbuf[0] = spi_instr | READ_SINGLE_BYTE;
  //rbuf[1] = 0;
  //wiringPiSPIDataRW (0, rbuf, len) ;
  //errata Section 3. You have to make sure that you read the same value of the register twice in a row before you evaluate it otherwise you might read a value that is a mix of 2 state values.
  rbuf[0] = spi_instr | READ_SINGLE_BYTE;
  rbuf[1] = 0;
  wiringPiSPIDataRW(0, rbuf, len);
  CC1101_status_FIFO_ReadByte = rbuf[0] & 0x0F;
  CC1101_status_state = (rbuf[0] >> 4) & 0x0F;
  value = rbuf[1];
  return value;
}

#define PATABLE_ADDR  			0x3E                                    // Pa Table Adress
#define TX_FIFO_ADDR		 	0x3F                            		
#define RX_FIFO_ADDR		 	0xBF                            		
void SPIReadBurstReg(uint8_t spi_instr, uint8_t *pArr, uint8_t len)
{
  uint8_t rbuf[len + 1];
  uint8_t i = 0;
  memset(rbuf, 0, len + 1);
  rbuf[0] = spi_instr | READ_BURST;
  wiringPiSPIDataRW(0, rbuf, len + 1);
  for (i = 0; i < len; i++)
  {
    pArr[i] = rbuf[i + 1];
    //echo_debug(debug_out,"SPI_arr_read: 0x%02X\n", pArr[i]);
  }
  CC1101_status_FIFO_ReadByte = rbuf[0] & 0x0F;
  CC1101_status_state = (rbuf[0] >> 4) & 0x0F;
}

void SPIWriteBurstReg(uint8_t spi_instr, uint8_t *pArr, uint8_t len)
{
  uint8_t tbuf[len + 1];
  uint8_t i = 0;
  tbuf[0] = spi_instr | WRITE_BURST;
  for (i = 0; i < len; i++)
  {
    tbuf[i + 1] = pArr[i];
    //echo_debug(debug_out,"SPI_arr_write: 0x%02X\n", tbuf[i+1]);
  }
  wiringPiSPIDataRW(0, tbuf, len + 1);
  CC1101_status_FIFO_FreeByte = tbuf[len] & 0x0F;
  CC1101_status_state = (tbuf[len] >> 4) & 0x0F;
}

/*---------------------------[CC1100-command strobes]----------------------------*/
#define SRES  					0x30                                    // Reset chip
#define SFSTXON  				0x31                                    // Enable/calibrate freq synthesizer
#define SXOFF  					0x32                                    // Turn off crystal oscillator.
#define SCAL 					0x33                                    // Calibrate freq synthesizer & disable
#define SRX  					0x34                                    // Enable RX.
#define STX  					0x35                                    // Enable TX.
#define SIDLE  					0x36                                    // Exit RX / TX
#define SAFC  					0x37                                    // AFC adjustment of freq synthesizer
#define SWOR  					0x38                                    // Start automatic RX polling sequence
#define SPWD  					0x39                                    // Enter pwr down mode when CSn goes hi
#define SFRX  					0x3A                                    // Flush the RX FIFO buffer.
#define SFTX  					0x3B                                    // Flush the TX FIFO buffer.
#define SWORRST  				0x3C                                    // Reset real time clock.
#define SNOP  					0x3D                                    // No operation.
/*----------------------------[END command strobes]------------------------------*/
void CC1101_CMD(uint8_t spi_instr)
{
  uint8_t tbuf[1] = { 0 };
  tbuf[0] = spi_instr | WRITE_SINGLE_BYTE;
  //echo_debug(debug_out,"SPI_data: 0x%02X\n", tbuf[0]);
  wiringPiSPIDataRW(0, tbuf, 1);
  CC1101_status_state = (tbuf[0] >> 4) & 0x0F;
}

void echo_cc1101_version(void);
void show_cc1101_registers_settings(void);

//---------------[CC1100 reset functions "200us"]-----------------------
void cc1101_reset(void)			// reset defined in cc1100 datasheet §19.1
{// CS should be high from gpio load spi command
  /* commented car ne fonctionne pas avec wiringPi a voir avec BCM2835 ..
     digitalWrite(cc1101_CSn, 0);     		// CS low
     pinMode (cc1101_CSn, OUTPUT);
     delayMicroseconds(30);
     digitalWrite(cc1101_CSn, 1);      	// CS high
     delayMicroseconds(100);	 // min 40us
  //Pull CSn low and wait for SO to go low
  digitalWrite(cc1101_CSn, 0);     		// CS low
  delayMicroseconds(30);
  */

  CC1101_CMD(SRES);	//GDO0 pin should output a clock signal with a frequency of CLK_XOSC/192.
  //periode 1/7.417us= 134.8254k  * 192 --> 25.886477M
  //10 periode 73.83 = 135.4463k *192 --> 26Mhz
  delay(1); //1ms for getting chip to reset properly

  CC1101_CMD(SFTX);   //flush the TX_fifo content -> a must for interrupt handling
  CC1101_CMD(SFRX);	//flush the RX_fifo content -> a must for interrupt handling	
}

void setMHZ(float mhz) {
  byte freq2 = 0;
  byte freq1 = 0;
  byte freq0 = 0;

  //Serial.printf("%.4f Mhz : ", mhz);

  for (bool i = 0; i == 0;) {
    if (mhz >= 26) {
      mhz -= 26;
      freq2 += 1;
    }
    else if (mhz >= 0.1015625) {
      mhz -= 0.1015625;
      freq1 += 1;
    }
    else if (mhz >= 0.00039675) {
      mhz -= 0.00039675;
      freq0 += 1;
    }
    else { i = 1; }
  }
  if (freq0 > 255) { freq1 += 1; freq0 -= 256; }

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
  // Rf settings for CC1101
  //
  halRfWriteReg(IOCFG2, 0x0D);  //GDO2 Output Pin Configuration : Serial Data Output
  halRfWriteReg(IOCFG0, 0x06);  //GDO0 Output Pin Configuration : Asserts when sync word has been sent / received, and de-asserts at the end of the packet.
  halRfWriteReg(FIFOTHR, 0x47); //0x4? adc with bandwith< 325khz
  halRfWriteReg(SYNC1, 0x55);   //01010101
  halRfWriteReg(SYNC0, 0x00);   //00000000 

  //halRfWriteReg(PKTCTRL1,0x80);//Preamble quality estimator threshold=16  ; APPEND_STATUS=0; no addr check
  halRfWriteReg(PKTCTRL1, 0x00);//Preamble quality estimator threshold=0   ; APPEND_STATUS=0; no addr check
  halRfWriteReg(PKTCTRL0, 0x00);//fix length , no CRC
  halRfWriteReg(FSCTRL1, 0x08); //Frequency Synthesizer Control

  setMHZ(freq);
  //halRfWriteReg(FREQ2,0x10);   //Frequency Control Word, High Byte  Base frequency = 433.82
  //halRfWriteReg(FREQ1,0xAF);   //Frequency Control Word, Middle Byte
  //halRfWriteReg(FREQ0, freq0);
  //halRfWriteReg(FREQ0,0x75); //Frequency Control Word, Low Byte la fréquence reel etait 433.790 (centre)
  //halRfWriteReg(FREQ0,0xC1); //Frequency Control Word, Low Byte rasmobo 814 824 (KO) ; minepi 810 820 (OK)
  //halRfWriteReg(FREQ0,0x9B); //rasmobo 808.5  -16  pour -38
  //halRfWriteReg(FREQ0,0xB7);   //rasmobo 810 819.5 OK
  //mon compteur F1 : 433809500  F2 : 433820000   deviation +-5.25khz depuis 433.81475M

  halRfWriteReg(MDMCFG4, 0xF6); //Modem Configuration   RX filter BW = 58Khz
  halRfWriteReg(MDMCFG3, 0x83); //Modem Configuration   26M*((256+83h)*2^6)/2^28 = 2.4kbps 
  halRfWriteReg(MDMCFG2, 0x02); //Modem Configuration   2-FSK;  no Manchester ; 16/16 sync word bits detected
  halRfWriteReg(MDMCFG1, 0x00); //Modem Configuration num preamble 2=>0 , Channel spacing_exp
  halRfWriteReg(MDMCFG0, 0x00); /*# MDMCFG0 Channel spacing = 25Khz*/
  halRfWriteReg(DEVIATN, 0x15);  //5.157471khz 
  //halRfWriteReg(MCSM1,0x0F);   //CCA always ; default mode RX
  halRfWriteReg(MCSM1, 0x00);   //CCA always ; default mode IDLE
  halRfWriteReg(MCSM0, 0x18);   //Main Radio Control State Machine Configuration
  halRfWriteReg(FOCCFG, 0x1D);  //Frequency Offset Compensation Configuration
  halRfWriteReg(BSCFG, 0x1C);   //Bit Synchronization Configuration
  halRfWriteReg(AGCCTRL2, 0xC7);//AGC Control
  halRfWriteReg(AGCCTRL1, 0x00);//AGC Control
  halRfWriteReg(AGCCTRL0, 0xB2);//AGC Control
  halRfWriteReg(WORCTRL, 0xFB); //Wake On Radio Control
  halRfWriteReg(FREND1, 0xB6);  //Front End RX Configuration
  halRfWriteReg(FSCAL3, 0xE9);  //Frequency Synthesizer Calibration
  halRfWriteReg(FSCAL2, 0x2A);  //Frequency Synthesizer Calibration
  halRfWriteReg(FSCAL1, 0x00);  //Frequency Synthesizer Calibration
  halRfWriteReg(FSCAL0, 0x1F);  //Frequency Synthesizer Calibration
  halRfWriteReg(TEST2, 0x81);   //Various Test Settings link to adc retention
  halRfWriteReg(TEST1, 0x35);   //Various Test Settings link to adc retention
  halRfWriteReg(TEST0, 0x09);   //Various Test Settings link to adc retention

  SPIWriteBurstReg(PATABLE_ADDR, PA, 8);
}

void  cc1101_init(float freq)
{
  pinMode(GDO0, INPUT_PULLUP);

  // to use SPI pi@MinePi ~ $ gpio unload spi  then gpio load spi   
  // sinon pas de MOSI ni pas de CSn , buffer de 4kB
  if ((wiringPiSPISetup(0, 500000)) < 0)        // channel 0 100khz   min 500khz ds la doc ?
  {
    //fprintf (stderr, "Can't open the SPI bus: %s\n", strerror (errno)) ;
    printf("Can't open the SPI bus");
    exit(EXIT_FAILURE);
  }
  cc1101_reset();
  delay(1); //1ms
  //echo_cc1101_version();
  //delay(1);
  //show_cc1101_registers_settings();
  //delay(1);
  cc1101_configureRF_0(freq);
}

int8_t cc1100_rssi_convert2dbm(uint8_t Rssi_dec)
{
  int8_t rssi_dbm;
  if (Rssi_dec >= 128)
  {
    rssi_dbm = ((Rssi_dec - 256) / 2) - 74;			//rssi_offset via datasheet
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
  CC1101_CMD(SIDLE);								//sets to idle first. must be in
  CC1101_CMD(SRX);									//writes receive strobe (receive mode)
  marcstate = 0xFF;									//set unknown/dummy state value
  while ((marcstate != 0x0D) && (marcstate != 0x0E) && (marcstate != 0x0F))							//0x0D = RX 
  {
    marcstate = halRfReadReg(MARCSTATE_ADDR);			//read out state of cc1100 to be sure in RX
  }
}

void echo_cc1101_version(void)
{
  echo_debug(debug_out, "CC1101 Partnumber: 0x%02X\n", halRfReadReg(PARTNUM_ADDR));
  echo_debug(debug_out, "CC1101 Version != 00 or 0xFF  : 0x%02X\n", halRfReadReg(VERSION_ADDR));  // != 00 or 0xFF
}

#define CFG_REGISTER  			0x2F									//47 registers
void show_cc1101_registers_settings(void)
{
  uint8_t config_reg_verify[CFG_REGISTER], Patable_verify[8];
  uint8_t i;

  memset(config_reg_verify, 0, CFG_REGISTER);
  memset(Patable_verify, 0, 8);

  SPIReadBurstReg(0, config_reg_verify, CFG_REGISTER);			//reads all 47 config register from cc1100	"359.63us"
  SPIReadBurstReg(PATABLE_ADDR, Patable_verify, 8);				//reads output power settings from cc1100	"104us"

  echo_debug(debug_out, "Config Register in hex:\n");
  echo_debug(debug_out, " 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");
  for (i = 0; i < CFG_REGISTER; i++) 		//showes rx_buffer for debug
  {
    echo_debug(debug_out, "%02X ", config_reg_verify[i]);

    if (i == 15 || i == 31 || i == 47 || i == 63)			//just for beautiful output style
    {
      echo_debug(debug_out, "\n");
    }
  }
  echo_debug(debug_out, "\n");
  echo_debug(debug_out, "PaTable:\n");

  for (i = 0; i < 8; i++) 					//showes rx_buffer for debug
  {
    echo_debug(debug_out, "%02X ", Patable_verify[i]);
  }
  echo_debug(debug_out, "\n");

}

uint8_t is_look_like_radian_frame(uint8_t* buffer, size_t len)
{
  int i, ret;
  ret = FALSE;
  for (i = 0; i < len; i++) {
    if (buffer[i] == 0xFF) ret = TRUE;
  }

  return ret;
}

//-----------------[check if Packet is received]-------------------------
uint8_t cc1101_check_packet_received(void)
{
  uint8_t rxBuffer[100];
  uint8_t l_nb_byte, l_Rssi_dbm, l_lqi, l_freq_est, pktLen;
  pktLen = 0;
  if (digitalRead(GDO0) == TRUE)
  {
    // get RF info at beginning of the frame
    l_lqi = halRfReadReg(LQI_ADDR);
    l_freq_est = halRfReadReg(FREQEST_ADDR);
    l_Rssi_dbm = cc1100_rssi_convert2dbm(halRfReadReg(RSSI_ADDR));

    while (digitalRead(GDO0) == TRUE)
    {
      delay(5); //wait for some byte received
      l_nb_byte = (halRfReadReg(RXBYTES_ADDR) & RXBYTES_MASK);
      if ((l_nb_byte) && ((pktLen + l_nb_byte) < 100))
      {
        SPIReadBurstReg(RX_FIFO_ADDR, &rxBuffer[pktLen], l_nb_byte); // Pull data
        pktLen += l_nb_byte;
      }
    }
    if (is_look_like_radian_frame(rxBuffer, pktLen))
    {
      echo_debug(debug_out, "\n");
      print_time();
      echo_debug(debug_out, " bytes=%u rssi=%u lqi=%u F_est=%u ", pktLen, l_Rssi_dbm, l_lqi, l_freq_est);
      show_in_hex_one_line(rxBuffer, pktLen);
      //show_in_bin(rxBuffer,l_nb_byte);		   
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
    delay(1); //in ms	
    //echo_cc1101_MARCSTATE();
    if (cc1101_check_packet_received()) //delay till system has data available
    {
      return TRUE;
    }
    else if (i == milliseconds - 1)
    {
      //echo_debug(debug_out,"no packet received!\n");
      return FALSE;
    }
  }
  return TRUE;
}

struct tmeter_data parse_meter_report(uint8_t *decoded_buffer, uint8_t size)
{
  struct tmeter_data data;
  if (size >= 30)
  {
    //echo_debug(1,"\n%u/%u/20%u %u:%u:%u ",decoded_buffer[24],decoded_buffer[25],decoded_buffer[26],decoded_buffer[28],decoded_buffer[29],decoded_buffer[30]);
    //echo_debug(1,"%u litres ",decoded_buffer[18]+decoded_buffer[19]*256 + decoded_buffer[20]*65536 + decoded_buffer[21]*16777216);

    data.liters = decoded_buffer[18] + decoded_buffer[19] * 256 + decoded_buffer[20] * 65536 + decoded_buffer[21] * 16777216;
  }
  if (size >= 48)
  {
    //echo_debug(1,"Num %u %u Mois %uh-%uh ",decoded_buffer[48], decoded_buffer[31],decoded_buffer[44],decoded_buffer[45]);
    data.reads_counter = decoded_buffer[48];
    data.battery_left = decoded_buffer[31];
    data.time_start = decoded_buffer[44];
    data.time_end = decoded_buffer[45];
  }
  return data;
}

// Remove the start- and stop-bits in the bitstream , also decode oversampled bit 0xF0 => 1,0
// 01234567 ###01234 567###01 234567## #0123456 (# -> Start/Stop bit)
// is decoded to:
// 76543210 76543210 76543210 76543210
uint8_t decode_4bitpbit_serial(uint8_t *rxBuffer, int l_total_byte, uint8_t* decoded_buffer)
{
  uint16_t i, j, k;
  uint8_t bit_cnt = 0;
  int8_t bit_cnt_flush_S8 = 0;
  uint8_t bit_pol = 0;
  uint8_t dest_bit_cnt = 0;
  uint8_t dest_byte_cnt = 0;
  uint8_t current_Rx_Byte;
  //show_in_hex(rxBuffer,l_total_byte);
  /*set 1st bit polarity*/
  bit_pol = (rxBuffer[0] & 0x80); //initialize with 1st bit state

  for (i = 0; i < l_total_byte; i++)
  {
    current_Rx_Byte = rxBuffer[i];
    //echo_debug(debug_out, "0x%02X ", rxBuffer[i]);
    for (j = 0; j < 8; j++)
    {
      if ((current_Rx_Byte & 0x80) == bit_pol) bit_cnt++;
      else if (bit_cnt == 1)
      { //previous bit was a glich so bit has not really change
        bit_pol = current_Rx_Byte & 0x80; //restore correct bit polarity
        bit_cnt = bit_cnt_flush_S8 + 1; //hope that previous bit was correctly decoded
      }
      else
      {  //bit polarity has change 
        bit_cnt_flush_S8 = bit_cnt;
        bit_cnt = (bit_cnt + 2) / 4;
        bit_cnt_flush_S8 = bit_cnt_flush_S8 - (bit_cnt * 4);

        for (k = 0; k < bit_cnt; k++)
        { // insert the number of decoded bit
          if (dest_bit_cnt < 8)
          { //if data byte
            decoded_buffer[dest_byte_cnt] = decoded_buffer[dest_byte_cnt] >> 1;
            decoded_buffer[dest_byte_cnt] |= bit_pol;
          }
          dest_bit_cnt++;
          //if ((dest_bit_cnt ==9) && (!bit_pol)){  echo_debug(debug_out,"stop bit error9"); return dest_byte_cnt;}
          if ((dest_bit_cnt == 10) && (!bit_pol)) { echo_debug(debug_out, "stop bit error10"); return dest_byte_cnt; }
          if ((dest_bit_cnt >= 11) && (!bit_pol)) //start bit
          {
            dest_bit_cnt = 0;
            //echo_debug(debug_out, " dec[%i]=0x%02X \n", dest_byte_cnt, decoded_buffer[dest_byte_cnt]);
            dest_byte_cnt++;
          }
        }
        bit_pol = current_Rx_Byte & 0x80;
        bit_cnt = 1;
      }
      current_Rx_Byte = current_Rx_Byte << 1;
    }//scan TX_bit
  }//scan TX_byte
  return dest_byte_cnt;
}

/*
   search for 0101010101010000b sync pattern then change data rate in order to get 4bit per bit
   search for end of sync pattern with start bit 1111111111110000b
   */
int receive_radian_frame(int size_byte, int rx_tmo_ms, uint8_t*rxBuffer, int rxBuffer_size)
{
  uint8_t  l_byte_in_rx = 0;
  uint16_t l_total_byte = 0;
  uint16_t l_radian_frame_size_byte = ((size_byte * (8 + 3)) / 8) + 1;
  int l_tmo = 0;
  uint8_t l_Rssi_dbm, l_lqi, l_freq_est;

  echo_debug(debug_out, "\nsize_byte=%d  l_radian_frame_size_byte=%d\n", size_byte, l_radian_frame_size_byte);

  if (l_radian_frame_size_byte * 4 > rxBuffer_size) { echo_debug(debug_out, "buffer too small\n"); return 0; }
  CC1101_CMD(SFRX);
  halRfWriteReg(MCSM1, 0x0F);   //CCA always ; default mode RX
  halRfWriteReg(MDMCFG2, 0x02); //Modem Configuration   2-FSK;  no Manchester ; 16/16 sync word bits detected   
  /* configure to receive beginning of sync pattern */
  halRfWriteReg(SYNC1, 0x55);   //01010101
  halRfWriteReg(SYNC0, 0x50);   //01010000	
  halRfWriteReg(MDMCFG4, 0xF6); //Modem Configuration   RX filter BW = 58Khz
  halRfWriteReg(MDMCFG3, 0x83); //Modem Configuration   26M*((256+83h)*2^6)/2^28 = 2.4kbps	
  halRfWriteReg(PKTLEN, 1); // just one byte of synch pattern
  cc1101_rec_mode();

  while ((digitalRead(GDO0) == FALSE) && (l_tmo < rx_tmo_ms)) { delay(1); l_tmo++; }
  if (l_tmo < rx_tmo_ms) echo_debug(debug_out, "GDO0! (0, %d) ", l_tmo); else return 0;
  while ((l_byte_in_rx == 0) && (l_tmo < rx_tmo_ms))
  {
    delay(5); l_tmo += 5; //wait for some byte received
    l_byte_in_rx = (halRfReadReg(RXBYTES_ADDR) & RXBYTES_MASK);
    if (l_byte_in_rx)
    {
      SPIReadBurstReg(RX_FIFO_ADDR, &rxBuffer[0], l_byte_in_rx); // Pull data
      //if (debug_out)show_in_hex_one_line(rxBuffer, l_byte_in_rx);
    }
  }
  if (l_tmo < rx_tmo_ms && l_byte_in_rx > 0) echo_debug(debug_out, "1st synch received (%d) ", l_byte_in_rx); else return 0;

  l_lqi = halRfReadReg(LQI_ADDR);
  l_freq_est = halRfReadReg(FREQEST_ADDR);
  l_Rssi_dbm = cc1100_rssi_convert2dbm(halRfReadReg(RSSI_ADDR));
  echo_debug(debug_out, " rssi=%u lqi=%u F_est=%u \n", l_Rssi_dbm, l_lqi, l_freq_est);

  fflush(stdout);
  halRfWriteReg(SYNC1, 0xFF);   //11111111
  halRfWriteReg(SYNC0, 0xF0);   //11110000 la fin du synch pattern et le bit de start
  halRfWriteReg(MDMCFG4, 0xF8); //Modem Configuration   RX filter BW = 58Khz
  halRfWriteReg(MDMCFG3, 0x83); //Modem Configuration   26M*((256+83h)*2^8)/2^28 = 9.59kbps
  halRfWriteReg(PKTCTRL0, 0x02); //infinite packet len
  CC1101_CMD(SFRX);
  cc1101_rec_mode();

  l_total_byte = 0;
  l_byte_in_rx = 1;
  while ((digitalRead(GDO0) == FALSE) && (l_tmo < rx_tmo_ms)) { delay(1); l_tmo++; }
  if (l_tmo < rx_tmo_ms) echo_debug(debug_out, "GDO0! (1, %d) ", l_tmo); else return 0;
  while ((l_total_byte < (l_radian_frame_size_byte * 4)) && (l_tmo < rx_tmo_ms))
  {
    delay(5); l_tmo += 5; //wait for some byte received
    l_byte_in_rx = (halRfReadReg(RXBYTES_ADDR) & RXBYTES_MASK);
    if (l_byte_in_rx)
    {
      //if (l_byte_in_rx + l_total_byte > (l_radian_frame_size_byte * 4))
      //  l_byte_in_rx = (l_radian_frame_size_byte * 4) - l_total_byte;

      SPIReadBurstReg(RX_FIFO_ADDR, &rxBuffer[l_total_byte], l_byte_in_rx); // Pull data
      l_total_byte += l_byte_in_rx;
    }
  }
  if (l_tmo < rx_tmo_ms && l_total_byte > 0) echo_debug(debug_out, "frame received (%d)\n", l_total_byte); else return 0;

  /*stop reception*/
  CC1101_CMD(SFRX);
  CC1101_CMD(SIDLE);
  //echo_debug(debug_out,"RAW buffer");
  //show_in_hex_array(rxBuffer,l_total_byte); //16ms pour 124b->682b , 7ms pour 18b->99byte
  /*restore default reg */
  halRfWriteReg(MDMCFG4, 0xF6); //Modem Configuration   RX filter BW = 58Khz
  halRfWriteReg(MDMCFG3, 0x83); //Modem Configuration   26M*((256+83h)*2^6)/2^28 = 2.4kbps
  halRfWriteReg(PKTCTRL0, 0x00); //fix packet len
  halRfWriteReg(PKTLEN, 38);
  halRfWriteReg(SYNC1, 0x55);   //01010101
  halRfWriteReg(SYNC0, 0x00);   //00000000
  return l_total_byte;
}

/*
   scenario_releve
   2s de WUP
130ms : trame interrogation de l'outils de reléve   ______------|...............-----
43ms de bruit
34ms 0101...01
14.25ms 000...000
14ms 1111...11111
83.5ms de data acquitement
50ms de 111111
34ms 0101...01
14.25ms 000...000
14ms 1111...11111
582ms de data avec l'index

l'outils de reléve doit normalement acquité
*/
struct tmeter_data get_meter_data(void)
{
  struct tmeter_data sdata;
  uint8_t marcstate = 0xFF;
  uint8_t wupbuffer[] = { 0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55 };
  uint8_t wup2send = 77;
  uint16_t tmo = 0;
  uint8_t rxBuffer[1000];
  int rxBuffer_size;
  uint8_t meter_data[200];
  uint8_t meter_data_size = 0;

  memset(&sdata, 0, sizeof(sdata));

  uint8_t txbuffer[100];
  Make_Radian_Master_req(txbuffer, METER_YEAR, METER_SERIAL);

  halRfWriteReg(MDMCFG2, 0x00);  //clear MDMCFG2 to do not send preamble and sync
  halRfWriteReg(PKTCTRL0, 0x02); //infinite packet len
  SPIWriteBurstReg(TX_FIFO_ADDR, wupbuffer, 8); wup2send--;
  CC1101_CMD(STX);	 //sends the data store into transmit buffer over the air
  delay(10); //to give time for calibration 
  marcstate = halRfReadReg(MARCSTATE_ADDR); //to  update 	CC1101_status_state
  echo_debug(debug_out, "MARCSTATE : raw:0x%02X  0x%02X free_byte:0x%02X sts:0x%02X sending 2s WUP...\n", marcstate, marcstate & 0x1F, CC1101_status_FIFO_FreeByte, CC1101_status_state);
  while ((CC1101_status_state == 0x02) && (tmo < TX_LOOP_OUT))					//in TX
  {
    if (wup2send)
    {
      if (wup2send < 0xFF)
      {
        if (CC1101_status_FIFO_FreeByte <= 10)
        { //this give 10+20ms from previous frame : 8*8/2.4k=26.6ms  temps pour envoyer un wupbuffer
          delay(20);
          tmo++; tmo++;
        }
        SPIWriteBurstReg(TX_FIFO_ADDR, wupbuffer, 8);
        wup2send--;
      }
    }
    else
    {
      delay(130); //130ms time to free 39bytes FIFO space
      SPIWriteBurstReg(TX_FIFO_ADDR, txbuffer, 39);
      if (debug_out && 0) {
        echo_debug(debug_out, "txbuffer:\n");
        show_in_hex_array(&txbuffer[0], 39);
      }
      wup2send = 0xFF;
    }
    delay(10); tmo++;
    marcstate = halRfReadReg(MARCSTATE_ADDR); //read out state of cc1100 to be sure in IDLE and TX is finished this update also CC1101_status_state
    //echo_debug(debug_out,"%ifree_byte:0x%02X sts:0x%02X\n",tmo,CC1101_status_FIFO_FreeByte,CC1101_status_state);			
  }
  echo_debug(debug_out, "%i free_byte:0x%02X sts:0x%02X\n", tmo, CC1101_status_FIFO_FreeByte, CC1101_status_state);
  CC1101_CMD(SFTX); //flush the Tx_fifo content this clear the status state and put sate machin in IDLE
  //end of transition restore default register
  halRfWriteReg(MDMCFG2, 0x02); //Modem Configuration   2-FSK;  no Manchester ; 16/16 sync word bits detected   
  halRfWriteReg(PKTCTRL0, 0x00); //fix packet len

  //delay(30); //43ms de bruit
  /*34ms 0101...01  14.25ms 000...000  14ms 1111...11111  83.5ms de data acquitement*/
  if (!receive_radian_frame(0x12, 150, rxBuffer, sizeof(rxBuffer))) echo_debug(debug_out, "TMO on REC\n");
  //delay(30); //50ms de 111111  , mais on a 7+3ms de printf et xxms calculs
  /*34ms 0101...01  14.25ms 000...000  14ms 1111...11111  582ms de data avec l'index */
  rxBuffer_size = receive_radian_frame(0x7C, 700, rxBuffer, sizeof(rxBuffer));
  if (rxBuffer_size)
  {
    if (debug_out) {
    //  echo_debug(debug_out, "rxBuffer:\n");
    //  show_in_hex_array(rxBuffer, rxBuffer_size);
    }

    meter_data_size = decode_4bitpbit_serial(rxBuffer, rxBuffer_size, meter_data);
    // show_in_hex(meter_data,meter_data_size);
    sdata = parse_meter_report(meter_data, meter_data_size);
  }
  else
  {
    echo_debug(debug_out, "TMO on REC\n");
  }
  return sdata;
}
