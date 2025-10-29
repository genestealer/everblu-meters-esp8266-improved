# Cyble RF 433MHz Water Meter - Technical Documentation

> Source: The WIKI of Maison Simon - Water Counter Documentation

## Overview

### Cyble RF 433MHz Module

The Actaris radio frequency module for water meters is a reliable, compact and intelligent tool for remote reading, from the distribution network to collective housing.

**Key Features:**
- Completely waterproof RADIAN™ compatible compact radio communication module
- Can be installed very easily, on site or in the factory
- Compatible with both cold and hot water meters
- Installation requires no wiring, wall fixing, meter removal or unplugging
- Designed to withstand harsh environments (flooded manholes to technical ducts)

**Intelligent Functions:**
- Index reading
- Monthly history of the index over 13 months
- Water return detection, reverse cumulative volume and 13-month monthly history
- Leak detection and 13-month monthly history
- Detection of fraud attempts
- End of battery life indication
- Various alarms

---

## RADIAN Protocol

> **Note:** The address http://www.radianprotocol.com/ is no longer active. Information retrieved from web.archive.org.

### Protocol Overview

**The Radian Protocol: A two-way 433 MHz Radio Protocol**

Designed for all applications in water, electricity, gas and heat meter reading and data transmitting.

**Key Characteristics:**

- **Reliability**
  - Physical layer: FSK modulation, narrow band
  - Logical Link Layer: packet numbering

- **Two-way, half duplex data transmission**

- **Relaying capability**
  - Receive and repeat datagrams
  - Up to 7 nodes forward

- **Sophisticated wake-up mechanism**
  - Standby mode
  - Wake-up signal → awaken mode
  - Address verification: "my address? yes → let's communicate! no → back to standby"
  - Time to get data from a node: 2-3 seconds

- **Both master-slave control and CSMA asynchronous communications**

### Technical Specifications

| Parameter | Value |
|-----------|-------|
| Process | FSK, bidirectional |
| Frequency | 433.82 MHz |
| Modulation | Asynchronous FSK, NRZ modulation |
| Protocol | Radian Protocol |
| Modulation shift | 5 kHz |
| Channel bandwidth | 25 kHz |
| Transmission speed | 2,400 baud |
| Central radio transmit power | +10 dBm (10 mW) |
| Reception sensitivity | -105 dBm |
| Range | approx. 50 m |

**Important Notes:**
- The counter can only be woken up during its "working hours" (configurable at factory)
- They mention cryptography (apart from CRC, there's minimal encryption)

---

## Protocol Details

### Physical Layer

#### RF Transmission

- **Process:** FSK, bidirectional
- **Frequency:** 433.82 MHz
- **Modulation:** Asynchronous FSK, NRZ modulation
- **Protocol:** Radian Protocol
- **Modulation shift:** 5 kHz
- **Channel bandwidth:** 25 kHz
- **Transmission speed:** 2,400 baud

#### Communication Frame Structure

Any communication frame consists of:

1. **Preamble** - notifies receiver that data will be sent
2. **Sync word** - notifies receiver that data transmission is starting
3. **Data**

### Preamble

Preamble is a series of `0101....0101` at 2400 bits/sec. Two preamble durations:

- **Long preamble** for meter wake-up: 4928 bits (2464 × 01)
- **Short preamble** for other frames: 80 bits (40 × 01)

> **Why long preamble?** To save energy, meter wakes up every 2 seconds to check if someone is speaking. If nobody is speaking, meter goes back to sleep.

### Sync Pattern

Sync pattern starts with:
- Low level during 14.3ms
- Followed by high level during 14.3ms

### Data Transmission

Data are sent by UART:

- **Baudrate:** 2400 bits/sec
- **Bit order:** LSB first
- **Format:** 1 start bit / No parity / 2 or 2.5 stop bits

---

## Frame Structure

| Field | Size | Description |
|-------|------|-------------|
| L | 1 byte | Length Byte - Total number of bytes including length byte and checksum |
| C | 1 byte | Control Byte (0x10: Request, 0x06: Acknowledge, 0x11: Response) |
| S | 1 byte | Spacer (0x00) |
| Receiver Address | 5 bytes | Meter address when master speaking, master address when meter speaking |
| S | 1 byte | Spacer (0x00) |
| Sender Address | 5 bytes | Master address when master speaking, meter address when meter speaking |
| S | 1 byte | Spacer (0x00) |
| Data Payload | 4-240 bytes | Up to 238 bytes of data |
| Checksum | 2 bytes | CRC-CCITT (Kermit) |

### Control Byte Values

- `0x10`: Request
- `0x06`: Acknowledge
- `0x11`: Response

### Checksum Calculation (CRC-CCITT Kermit)

- **Polynomial:** 0x8408
- **Initial Value:** 0
- **Bytes are reversed** (MSB first)
- **Result is inverted**
- **Final XOR:** 0
- **Stored in:** Little endian

---

## Meter Address Encoding

Address is encoded over 5 bytes:

1. **First byte:** `0x45` (to be confirmed)
2. **Four other bytes:** Deduced from numbers below the bar-code

### Bar-code Format: `YY-AAAAAAA-CCC`

- **YY:** Years encoded on 8 bits
- **AAAAAAA:** Serial number (convert from decimal to hex, MSB first)
- **CCC:** Check digits (not used in address encoding, but used to verify YY-AAAAAAA consistency)

### Example

Serial number: `16-0123456-CCC`

- YY = 16d → `0x10`
- AAAAAAA = 0123456d → `0x01E240`

**Resulting address:** `45 10 01 E2 40`

---

## Communication Example

### Master Request

Must be preceded by 2 seconds of `2464*01` then followed by Sync pattern and encapsulated in 1 start bit / No parity / 2.5 stop bits.

Example for meter `16-0123456`:

```
13 10 00 45 10 01 E2 40 00 45 67 89 AB CD 00 0A 40 DA DC
```

| Field | Value | Description |
|-------|-------|-------------|
| Length | 13 | Frame length |
| Control | 10 | Request |
| Spacer | 00 | - |
| Receiver Address | 45 10 01 E2 40 | Meter address |
| Spacer | 00 | - |
| Sender Address | 45 67 89 AB CD | Master address |
| Spacer | 00 | - |
| Data + Checksum | 0A 40 DA DC | Data and CRC |

### Communication Sequence

1. **Master request:** `13 10 00 45 10 01 E2 40 00 45 67 89 AB CD 00 0A 40 DA DC`
2. **Meter Acknowledge:** `12 06 00 45 67 89 AB CD 00 45 10 01 E2 40 00 0A 90 9E`
3. **Meter response:** `7C 11 00 45 67 89 AB CD 00 45 10 01 E2 40 00 01 08 00 D2 73 07 00 40 ….. cks`
   - Example shows 488,402 liters
4. **Master Acknowledge:** `12 06 00 45 10 01 E2 40 00 45 67 89 AB CD 00 0A 23 93`

> **Note:** For CRC calculation, use the Kermit algorithm and swap the nibbles → http://crccalc.com/

---

## Hardware Implementation

### CC1101 Transceiver

The CC1101 is an RF transceiver where you can adjust many parameters, but the actual frequency is not exactly the one you set.

**Important:** The actual frequency may vary between different CC1101 modules. Calibration is required using an SDR dongle.

#### Frequency Calibration Example

```c
halRfWriteReg(FREQ0, 0xC1);  // CC1101_N1: 814-824 kHz (not working)
halRfWriteReg(FREQ0, 0xB7);  // CC1101_N1: 810-819.5 kHz (OK)
```

Example measured frequencies:
- F1: 433.8085 MHz
- F2: 433.8195 MHz

**Calibration Process:**
1. Start with base frequency
2. Measure with SDR dongle to center on 433.820 MHz or the meter's response frequency
3. If no response, shift in 2kHz steps on each side of 433.820 MHz
4. Modify FREQ0 by adding/subtracting a few units to shift by 2kHz

---

### Raspberry Pi + CC1101 Setup

#### Cable Schematics

```
5V, GND ---> RPi HE26 --- . ----- 2 × GND; 2 × 3.3V, SCLK, MISO, MOSI, CSn, GDO2, GDO0 --------> CC1101 HE10
                         |
                      debug_connector (HE14)
```

#### RPi PIN Allocation (WiringPi Pin = WPP)

| Function | WPP | Name | Header | Name | WPP | Function |
|----------|-----|------|--------|------|-----|----------|
| | | 3.3v | 1 | 2 | 5v | |
| | 8 | SDA | 3 | 4 | 5v | |
| | 9 | SCL | 5 | 6 | 0v | |
| | 7 | GPIO7 | 7 | 8 | TxD | 15 |
| | | 0v | 9 | 10 | RxD | 16 |
| GDO0 | 0 | GPIO0 | 11 | 12 | GPIO1 | 1 |
| GDO2 | 2 | GPIO2 | 13 | 14 | 0v | |
| LED | 3 | GPIO3 | 15 | 16 | GPIO4 | 4 |
| | | 3.3v | 17 | 18 | GPIO5 | 5 |
| MOSI | 12 | MOSI | 19 | 20 | 0v | |
| MISO | 13 | MISO | 21 | 22 | GPIO6 | 6 |
| SCLK | 14 | SCLK | 23 | 24 | CE0 | 10 | Csn |
| GND | | 0v | 25 | 26 | CE1 | 11 |

#### Pin Definitions

```c
#define GDO2 2          // header 13
#define GDO1_MISO 13
#define GDO0 0          // header 11
#define MOSI 12
#define cc1101_CSn 10   // header 24
#define LED 3           // header 15
```

#### CC1101 HE10 Connector (Top View)

| Function | Header | Function |
|----------|--------|----------|
| 3.3v | 1 | 2 | 3.3V |
| MOSI | 3 | 4 | SCLK |
| MISO | 5 | 6 | GDO2 |
| CSn | 7 | 8 | GDO0 |
| GND | 9 | 10 | GND |

#### CC1101 HE10 Connector (Flipped View from Bottom)

| Function | Header | Function |
|----------|--------|----------|
| 3.3v | 2 | 1 | 3.3V |
| MOSI | 4 | 3 | SCLK |
| MISO | 6 | 5 | GDO2 |
| CSn | 8 | 7 | GDO0 |
| GND | 10 | 9 | GND |

---

### Code Configuration (radian_trx.zip)

> **Note:** The zip password is `d3 pa5se` (name of the zip file: `radian_trx.zip`)

#### Required Modifications

The delivered code needs adjustments to compile (`gcc radian_trx.c -o radian_trx -lwiringPi -lpthread -Wall`):

1. **Frequency adjustment** in `CC1101.c`, line 229:
   ```c
   halRfWriteReg(FREQ0, ....);
   ```

2. **Meter serial number** in `CC1101.c`, line 664:
   ```c
   TS_len_u8 = Make_Radian_Master_req(txbuffer, 16, 123456);
   ```
   Format: `Make_Radian_Master_req(txbuffer, YY, AAAAAAA)`

3. **Add define** at top of `CC1101.c`:
   ```c
   #define TX_LOOP_OUT 300
   ```

4. **Fix typo** in `radian_trc.c`, line 5: Delete a "c" character

#### Cron Script Setup

```bash
sudo crontab -e
```

Add one of these lines:

```bash
# Silent mode (no logging)
55 9 * * * sudo /home/pi/radian_trx/web_tx_releve >/dev/null 2>&1

# With logging
55 9 * * * sudo /home/pi/radian_trx/web_tx_releve >> /var/log/crontab.log
```

#### Performance Results

| Configuration | Shutter | RSSI | LQI | F_est |
|---------------|---------|------|-----|-------|
| minepi + CC1101(2) + λ/4 behind partition | open | 185 | 128 | 255 |
| minepi + CC1101(2) + λ/4 in partition | open | 185 | 128 | 255 |
| minepi + CC1101(2) + spiral antenna behind partition | open | 183-184 | 128 | 255 |

---

## RTL-SDR for Reception Analysis

### Setup

1. Plug NESDR into an available USB port
2. Open 'NESDR Driver Installer' (Zadig)
3. Select 'List All Devices' from Options menu
4. From dropdown, select the NESDR
5. Confirm selected device has USB ID `0BDA 2838`
6. Press the big button to install drivers

### Resources

- [SDR# Quick Start Guide](http://www.rtl-sdr.com/rtl-sdr-quick-start-guide/)
- [RTL-SDR Kalibrate](http://rtl-sdr.sceners.org/?p=193)
- [YouTube Tutorial](https://www.youtube.com/watch?v=c3C7GBuxpNo)

### Recording Data

With a sampling frequency of 31.25 ksps:
- 2.5GB of data over 12 hours
- On a 1TB HDD: 1024/2.5 = 410 working days of recordings

---

## Antenna Information

### Antenna Types (Ordered by Gain - Least to Best)

1. Monopoly 1/2 Wave
2. 1/4 wave monopoly
3. 1/2 wave dipole
4. 1/4 wave dipole (approx 70Ω)
5. 1/2 wave inverted V antenna
6. 1/4 wave inverted V antenna
7. Moxon antenna (approx 50Ω)
8. Yagi antenna, patch or quad (directional antenna)

### Resources

- [Antenna Theory PDF](http://www.ta-formation.com/cours/e-antennes.pdf)
- [Ground Plane 446 MHz](https://www.adri38.fr/antenne-ground-plane-446-mhz/)
- [Hentenna](http://iw3hzx.altervista.org/Antenna/HENTENNA/Hentenna.htm)
- [Moxon Calculator](http://www.qsl.net/ve2ztt/IndexD/moxon_fichiers/moxon.htm)
- [Various Antenna Designs](http://f5ad.free.fr/ANT-QSP_Descriptions_430.htm)

---

## Hacking Timeline

- **2011:** Initial web page released
- **2014:** Julien joined, started collecting documentation
- **February 2016:** Captured several meter readings using SDR and large HDD (24/7 recording)
- **December 2016:** Successfully captured own meter readings
- **January 2017:** SigmaPic managed to read meter successfully

### Key Challenges Solved

1. CRC calculation method (CRC-CCITT Kermit)
2. Linking meter label to reading frame
3. Bit alignment and decoding
4. Address encoding scheme

---

## Additional Hardware References

- [FSK Emitter 433.92MHz](http://fr.farnell.com/mipot/32000508e/emitter-fsk-50-pll-5-12v-433-92mhz/dp/1702924)
  - Frequency range: 433.42MHz to 434.42MHz

---

## Related Documentation

- **Standard 13757-4** - Not fully compatible with Radian protocol
- **open-meter_wp2_d2.1_part3_v1.0.pdf** - EverBlu section (5.4.11) mentions 10Kbps but is a false lead
- **f5943_cyblerfvacatris.pdf** - Cyble RF technical file (limited additional information)

---

*This documentation is based on community research and reverse engineering efforts.*
