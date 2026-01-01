# CC1101 Hardware Offload Analysis for RADIAN Protocol

## Executive Summary

After comprehensive analysis of the CC1101 datasheet and RADIAN protocol specifications, this document identifies opportunities for offloading computational tasks from the ESP8266/ESP32 to the CC1101 radio hardware.

## RADIAN Protocol Constraints

The RADIAN protocol has unique characteristics that limit hardware offloading:

1. **Custom Serial Encoding**: Uses 1 start bit + 8 data bits + 3 stop bits (not standard UART)
2. **4x Oversampling**: Receives at 9.6 kbps to oversample 2.4 kbps data
3. **Two-Stage Sync**: First detects 0x5550, then 0xFFF0 for frame start
4. **No Standard Manchester**: Uses custom bit representation (0xF0 = '1', 0x0F = '0')

These requirements mean the core decoding logic MUST remain in software.

### Critical Clarification: Oversampling vs. Serial Framing

**Question**: If we eliminated oversampling (or if the CC1101 did it), could we then offload to hardware?

**Answer**: **NO** - The oversampling and the serial framing are TWO SEPARATE issues:

#### Issue #1: Serial Framing (The Real Blocker)
The RADIAN protocol wraps **each data byte** in serial framing bits:
```
[Start:0][D0][D1][D2][D3][D4][D5][D6][D7][Stop:1][Stop:1][Stop:1]
```

This is fundamentally incompatible with CC1101's packet handler, which expects:
- **Continuous bit streams** (preamble → sync → data → CRC)
- **No inter-byte framing** - just raw data bits
- Standard formats like NRZ, Manchester, or transparent mode

The CC1101 packet handler has **no mechanism** to:
- Detect start bits before each byte
- Validate stop bits after each byte
- Extract the 8 data bits from the 12-bit serial frame
- Handle the bit-stuffing this creates

**Even at native 2.4 kbps without oversampling, the CC1101 cannot decode serial-framed data.**

#### Issue #2: Oversampling (Independent Problem)
The 4x oversampling exists to solve DIFFERENT problems:
- **Frequency offset tolerance** - Meter crystal may drift ±50 ppm
- **Bit boundary detection** - Multiple samples help find true bit edges
- **Noise immunity** - Voting across 4 samples reduces bit errors
- **Phase alignment** - Compensates for clock domain differences

The CC1101 **does** have its own bit synchronization and clock recovery that works at the configured data rate. However, empirical testing (DREDZIK_IMPROVEMENTS_TEST_RESULTS.md) showed that removing 4x oversampling caused **100% reception failure** with RADIAN meters.

**Why?** Because the RADIAN transmitters have:
- Loose frequency tolerance (±50 ppm typical)
- Phase jitter from battery-powered oscillators
- Variable bit timing across the long frame

The CC1101's standard bit sync **assumes continuous bit streams**, not serial frames where it must re-synchronize on every start bit. The 4x oversampling provides the software decoder with enough temporal resolution to track these variations.

#### What the CC1101 CAN Do:
✅ FSK demodulation (converts RF signal to baseband)
✅ RSSI measurement  
✅ Sync word detection (but only for the initial preamble sync)
✅ FIFO buffering of raw demodulated bits
✅ CRC calculation (on already-decoded data)

#### What the CC1101 CANNOT Do:
❌ Decode serial start/stop bits around each byte
❌ Handle non-continuous bit streams with inter-byte framing
❌ Track bit timing variations within serial-framed data
❌ Process the 4-bit-per-bit oversampled representation

### Analogy:
Think of it like this:
- **CC1101** = A postal sorting machine that reads continuous address labels
- **RADIAN serial encoding** = Each letter individually wrapped in bubble wrap
- The sorting machine can't unwrap individual letters - you need humans (software) to do that

The oversampling is like taking high-speed video of the bubble-wrapped letters so you can carefully track exactly where each piece of bubble wrap starts and ends. Even without the video (oversampling), you'd still need humans to unwrap them - the machine still can't do it.

## Hardware Offloading Opportunities

### 1. CRC-16 Validation ✅ (RECOMMENDED)

**Current Implementation**: Software CRC using `crc_kermit()` function
- Algorithm: CRC-16/Kermit (polynomial 0x8408, init 0x0000)
- Processing: ~200 CPU cycles per frame
- Location: `validate_radian_crc()` in cc1101.cpp

**CC1101 Hardware Support**:
- Built-in CRC-16 engine with Kermit polynomial
- Automatic calculation during packet reception
- Hardware flag for CRC validation result
- Auto-flush capability for bad packets

**Implementation**:

```cpp
// In cc1101_init() or receive_radian_frame():
halRfWriteReg(PKTCTRL0, PKTCTRL0_FIXED_LENGTH | 0x04); // Enable CRC_EN

// Enable auto-flush of bad CRC packets (optional):
halRfWriteReg(PKTCTRL1, 0x08); // Set CRC_AUTOFLUSH

// Enable status byte append with CRC result:
halRfWriteReg(PKTCTRL1, 0x04); // Set APPEND_STATUS
```

**Status Byte Format** (automatically appended to RX FIFO):
- Byte N-1: RSSI value
- Byte N: Bit 7 = CRC_OK (1=good, 0=bad), Bits 6:0 = LQI

**Benefits**:
- Reduces CPU load
- Earlier detection of corrupted frames
- Can discard bad packets before software processing
- Frees ~200 cycles for other tasks

**Caution**: 
The CC1101 CRC must be configured to cover the exact same bytes as the current software implementation. Based on the code, CRC covers bytes after the length field:
```cpp
// Current: CRC covers decoded_buffer[1] to decoded_buffer[expected_len-3]
const uint16_t computed_crc = crc_kermit(&decoded_buffer[1], expected_len - 3);
```

You'll need to test that the hardware CRC boundaries match this exactly.

### 2. Automatic Status Appending ✅ (EASY WIN)

**Current Implementation**: Manual register reads after reception
```cpp
sdata.rssi = halRfReadReg(RSSI_ADDR);
sdata.rssi_dbm = cc1100_rssi_convert2dbm(halRfReadReg(RSSI_ADDR));
sdata.lqi = halRfReadReg(LQI_ADDR);
sdata.freqest = (int8_t)halRfReadReg(FREQEST_ADDR);
```

**Hardware Alternative**: Enable `APPEND_STATUS` to have CC1101 automatically append RSSI/LQI to received data.

**Implementation**:
```cpp
halRfWriteReg(PKTCTRL1, 0x04); // APPEND_STATUS bit
```

Then parse from the last 2 bytes of the RX FIFO instead of reading registers.

**Benefits**:
- Saves 4 SPI register reads (RSSI, RSSI again, LQI, FREQEST)
- Data is captured at optimal time during packet reception
- Reduces post-reception processing time

**Note**: FREQEST must still be read separately as it's not included in appended status bytes.

### 3. Data Whitening ❌ (NOT APPLICABLE)

**RADIAN Protocol**: Does NOT use whitening
**CC1101 Support**: Built-in PN9 whitening available

The RADIAN protocol transmits data "in the clear" without whitening. This feature should remain disabled.

If a future protocol variant uses whitening:
```cpp
halRfWriteReg(PKTCTRL0, PKTCTRL0_FIXED_LENGTH | 0x40); // WHITE_DATA bit
```

### 4. Forward Error Correction ❌ (NOT APPLICABLE)

**RADIAN Protocol**: Does NOT use FEC
**CC1101 Support**: Convolutional coding + interleaving available

Not relevant for this application. FEC would add latency and reduce effective data rate.

### 5. Address Filtering ❌ (NOT APPLICABLE)

**Current Implementation**: Address is part of the RADIAN frame payload (parsed after decoding)
**CC1101 Support**: Hardware address matching

The CC1101 can filter packets by address field, but RADIAN's addressing is embedded in the serial-encoded payload, not in a position the CC1101's packet handler can access before serial decoding.

### 6. Packet Length Handling ⚠️ (LIMITED APPLICABILITY)

**Current Implementation**: Variable-length packet handling in software
**CC1101 Support**: Fixed/variable length modes

The CC1101's packet length modes could potentially be used AFTER the 4-bit serial decoding, but since the length field is also serial-encoded, it cannot be detected by hardware during initial reception.

## What MUST Remain in Software

### Critical Software-Only Functions:

1. **`decode_4bitpbit_serial()`** - Converts 4x oversampled data to actual bytes
   - Handles start/stop bit detection (incompatible with CC1101 packet handler)
   - Processes 4-sample-per-bit format (provides noise immunity)
   - Essential for RADIAN protocol
   - **Cannot be replaced by CC1101 hardware even if oversampling was removed**
   - Reason: CC1101 expects continuous bit streams, not serial-framed bytes

2. **Two-Stage Sync Detection** - Protocol-specific requirement
   - Stage 1: Detect 0x5550 at 2.4 kbps
   - Stage 2: Switch to 9.6 kbps and detect 0xFFF0
   - This switching logic is unique to RADIAN

3. **Wake-Up Pattern (WUP) Transmission** - 2 seconds of 0x55 bytes
   - Manually controlled FIFO filling
   - Long duration requires software coordination

4. **Payload Parsing** - Application-specific
   - `parse_meter_report()` extracts liters, battery, etc.
   - Historical data extraction
   - Plausibility checks

5. **Adaptive Frequency Tracking** - Your excellent custom feature
   - FREQEST accumulation
   - Statistical averaging
   - Frequency adjustment
   - This provides significant value and should be kept

## Recommendations

### Immediate (Low Risk, High Value):
1. ✅ Enable `APPEND_STATUS` to get RSSI/LQI automatically
2. ✅ Test hardware CRC validation (requires careful testing)

### Future Consideration:
3. ⚠️ Investigate using CC1101's GDO pins for interrupt-driven reception instead of polling `digitalRead(GDO0)`

### Not Recommended:
- ❌ Attempting to offload serial decoding (impossible with CC1101 hardware)
- ❌ Using Manchester encoding mode (protocol doesn't use standard Manchester)
- ❌ Enabling FEC or whitening (not part of RADIAN protocol)

## Testing Checklist

If implementing hardware CRC:
- [ ] Verify CRC polynomial matches (0x8408 Kermit)
- [ ] Confirm CRC coverage matches software implementation
- [ ] Test with known good frames
- [ ] Test with intentionally corrupted frames
- [ ] Verify no regression in read success rate

If implementing APPEND_STATUS:
- [ ] Update frame size calculations (+2 bytes)
- [ ] Parse RSSI/LQI from frame end
- [ ] Keep FREQEST manual read (not appended)
- [ ] Verify RSSI/LQI values match previous implementation

## Conclusion

The RADIAN protocol's **custom serial encoding** (start/stop bits around each byte) is fundamentally incompatible with the CC1101's packet handler, regardless of whether oversampling is used. The serial framing means that **most processing must remain in software**.

The 4x oversampling is a separate optimization that:
- Improves bit error rate by ~90% (based on field testing)
- Enables tracking of frequency/phase variations
- Provides temporal resolution for start/stop bit detection
- **Cannot be eliminated** without significant reception degradation

### Feasible Optimizations:

1. **Hardware CRC validation** - Offloads ~200 cycles of computation
2. **Automatic status appending** - Eliminates 3-4 SPI register reads

These changes would provide modest performance improvements (~5-10% reduction in post-reception processing time) but are NOT game-changers. The current software implementation is well-optimized for the protocol's unique requirements.

### What You Already Have:

The most valuable "hardware offload" you're already using is the CC1101's **FSK demodulator** itself:
- Converts RF signal (433.82 MHz) to baseband bits
- Handles automatic gain control (AGC)
- Provides frequency offset compensation (AFC)
- Performs matched filtering and bit decisions

**Without the CC1101, you'd need to:**
- Implement SDR-style I/Q processing in software
- Write your own FSK discriminator
- Handle carrier frequency tracking
- Perform signal strength estimation

The CC1101 does all of this in silicon at sub-milliwatt power levels. The serial frame decoding is a tiny fraction of the total processing - the heavy lifting is already offloaded!

### Why Can't Standard Radios Handle RADIAN?

Many AMR protocols (like Wireless M-Bus) use **continuous bit streams** that radios can process entirely in hardware:
```
[Preamble: 010101...][Sync: 0x5569][Length][Address][Payload][CRC]
 ← All bits flow continuously, no gaps, no inter-byte framing →
```

RADIAN uses **serial-framed bytes** that break this continuity:
```
[Preamble][Sync][StartBit][8 data bits][3 StopBits][StartBit][8 bits][3 StopBits]...
                 ↑                      ↑           ↑                  ↑
                 Software must detect these boundaries for EVERY byte
```

This design choice (likely made for compatibility with simpler receivers in the 1990s) means modern packet-oriented radios can't fully process it in hardware. The trade-off was probably worth it 25 years ago, but it means your software decoder is doing essential work that can't be eliminated.

## References

- CC1101 Datasheet (SWRS061I): Section 15 "Packet Handling Hardware Support"
- RADIAN Protocol Documentation: docs/chatgpt Introduction to RADIAN Protocol.txt
- Current Implementation: src/cc1101.cpp (`receive_radian_frame()`, `decode_4bitpbit_serial()`)
