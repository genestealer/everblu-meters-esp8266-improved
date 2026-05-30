# GDO2 Hardware TX FIFO Threshold Management

**Implements:** GitHub Issue #83 — Hardware-accelerated FIFO management to eliminate `TXFIFO_UNDERFLOW` errors  
**Branch:** `feature/gdo2-fifo-threshold`

---

## Background: The TXFIFO_UNDERFLOW Problem

When communicating with an EverBlu Cyble meter, the firmware transmits a sequence of frames into the CC1101's 64-byte TX FIFO:

1. Multiple 8-byte **Wake-Up Packets (WUP)** — repeated ~25 times to wake the meter
2. One 39-byte **interrogation frame** — requests the meter reading

The CC1101 continuously drains the TX FIFO at the configured data rate (2.4 kbps for the RADIAN protocol). The firmware must refill the FIFO fast enough to avoid emptying it completely, which causes an irreversible **TXFIFO_UNDERFLOW** state (`MARCSTATE = 0x16`) that silently discards all further TX data.

### Why the Original SPI Polling Approach Fails Under ESPHome

The original code polled the TX FIFO level by reading the `TXBYTES` register over SPI:

```cpp
// Original approach — unreliable under ESPHome scheduler
if (CC1101_status_FIFO_FreeByte <= 10) {
    delay(20);  // Wait for FIFO to drain a bit
}
```

Two compounding problems:

| Problem | Effect |
|---------|--------|
| `CC1101_status_FIFO_FreeByte` is the **status byte from the previous SPI transaction**, not a fresh TXBYTES read | Stale data; does not reflect current FIFO level |
| `delay(20)` in ESPHome yields to the ESP-IDF/FreeRTOS scheduler | WiFi, mDNS, OTA, API tasks can stall it for **10–80 ms** — far exceeding the time to drain a 64-byte FIFO at 2.4 kbps (≈213 ms total) |

The result: the FIFO empties mid-transmission, CC1101 enters `TXFIFO_UNDERFLOW`, and the meter never receives a valid wake sequence.

---

## Part 1: GDO2 as Hardware TX FIFO Threshold Signal

### How GDO2 Works

The CC1101 `GDO2` output pin is configurable via register `IOCFG2`. Setting `IOCFG2 = 0x02` configures GDO2 as:

> **TX FIFO at or above threshold → GDO2 asserts HIGH**  
> **TX FIFO below threshold → GDO2 de-asserts LOW**

This is a hardware signal, updated by the CC1101's internal logic on every byte drained — no SPI transaction required to read it.

```
digitalRead(GDO2)   ~1 µs  (GPIO register read)
halRfReadReg(TXBYTES)  ~10 µs  (SPI: CS assert + 2 bytes + CS de-assert)
```

More importantly, `digitalRead()` is immune to ESP scheduler stalls — it reads a hardware register synchronously.

### FIFOTHR Register Selection

The `FIFOTHR` register (address `0x03`) controls both the TX and RX threshold levels. The key insight is that when GDO2 **de-asserts** (FIFO falls below threshold), the number of **free** bytes in the FIFO is:

```
free_bytes = 64 - TX_threshold
```

For reliable single-check operation:
- **8-byte WUP buffer**: needs 8 free bytes
- **39-byte interrogation frame**: needs 39 free bytes

We need `64 - TX_threshold ≥ 39`, so `TX_threshold ≤ 25`.

| `FIFOTHR` value | TX threshold | Free bytes on de-assert | Notes |
|-----------------|--------------|-------------------------|-------|
| `0x47` (FIFO_THR=7) | 33 bytes | 32 bytes | ❌ Not enough for 39-byte frame |
| `0x49` (FIFO_THR=9) | 25 bytes | **40 bytes** | ✅ Fits WUP (8) and frame (39) |

**Setting used:** `FIFOTHR = 0x49`

> **Note:** This shifts the RX FIFO threshold from 33 bytes to 40 bytes, but this has no functional impact since the RX loop uses `GDO0` (sync-word detect) plus `RXBYTES` polling, not the FIFO threshold signal.

### WUP Feeding Loop (TX Phase)

```cpp
// GDO2 path: hardware signal — immune to scheduler stalls
if (GET_GDO2_PIN() >= 0) {
    if (digitalRead(GET_GDO2_PIN()) == LOW) {
        // FIFO < 25 bytes → ≥40 free bytes guaranteed → safe to write 8-byte WUP
        SPIWriteBurstReg(TX_FIFO_ADDR, wupbuffer, 8);
        wup2send--;
    }
    // else: FIFO still above threshold — skip write this iteration
} else {
    // Fallback: stale FreeByte check + delay(20) (original behaviour)
    if (CC1101_status_FIFO_FreeByte <= 10) {
        delay(20);
    }
    SPIWriteBurstReg(TX_FIFO_ADDR, wupbuffer, 8);
    wup2send--;
}
```

### Interrogation Frame Wait (TX Phase)

```cpp
if (GET_GDO2_PIN() >= 0) {
    // Wait until GDO2 goes LOW (FIFO < 25 bytes → ≥40 free bytes → fits 39-byte frame)
    int wait_count = 0;
    while (digitalRead(GET_GDO2_PIN()) == HIGH && wait_count < 100) { // safety ~500ms
        delay(5);
        wait_count++;
    }
    // Final underflow guard before writing
    uint8_t txbytes_reg = halRfReadReg(TXBYTES_ADDR);
    if (txbytes_reg & 0x80) break; // Overflow flag set — abort
    SPIWriteBurstReg(TX_FIFO_ADDR, buffer, length);
} else {
    // Fallback: TXBYTES SPI poll (original behaviour)
    while (halRfReadReg(TXBYTES_ADDR) > 25) { delay(5); }
    SPIWriteBurstReg(TX_FIFO_ADDR, buffer, length);
}
```

---

## Part 2: RX FIFO Threshold (Future Work)

The RX FIFO overflow risk is lower in priority:

- The EverBlu meter response is ≈200 bytes at 2.4 kbps → takes ≈667 ms to arrive
- The 64-byte RX FIFO takes ≈213 ms to fill
- The RX loop calls `delay(2)` between reads, yielding to the scheduler

A future enhancement (separate issue) could reconfigure `IOCFG2 = 0x01` (RX FIFO at/above threshold **or** end-of-packet) during the RX phase to trigger `yield()` more precisely. This would require dynamic IOCFG2 reconfiguration between TX and RX phases.

---

## Part 3: Dynamic IOCFG2 Reconfiguration (Future Work)

The current implementation configures `IOCFG2 = 0x02` (TX FIFO threshold) permanently at init time. A more sophisticated approach would be:

1. **TX phase**: `IOCFG2 = 0x02` (TX FIFO threshold, current implementation)
2. **RX phase**: `IOCFG2 = 0x01` (RX FIFO at/above threshold OR end-of-packet)

This would allow GDO2 to serve double duty — but requires a single SPI write at the TX→RX transition and careful handling of the GDO2 signal polarity change.

---

## Part 4: Implementation Scope

| File | Change | Scope |
|------|--------|-------|
| `src/core/cc1101.cpp` | IOCFG2 = TX_FIFO_THR; FIFOTHR = 0x49; GDO2 pin variable/setter/macro; init pinMode; TX WUP loop; TX frame wait | Both (MQTT + ESPHome) |
| `src/core/cc1101.h` | `cc1101_set_gdo2_pin()` declaration | ESPHome only |
| `include/private.example.h` | Optional `#define GDO2 <pin>` | MQTT/PlatformIO only |
| `src/main.cpp` | GDO2 pin reporting in startup validation | MQTT/PlatformIO only |
| `ESPHOME/components/everblu_meter/everblu_meter.h` | `set_gdo2_pin()` setter + `gdo2_pin_` member | ESPHome only |
| `ESPHOME/components/everblu_meter/everblu_meter.cpp` | `apply_radio_context()` + `dump_config()` | ESPHome only |
| `ESPHOME/components/everblu_meter/__init__.py` | `CONF_GDO2_PIN`, optional schema entry, `to_code()` | ESPHome only |
| `ESPHOME/example-*.yaml` | Commented `gdo2_pin:` entries | ESPHome examples |
| `ESPHOME-release/` | Regenerated via `prepare-component-release.ps1` | Generated output |

### Backward Compatibility

GDO2 is **entirely optional**:

- `GET_GDO2_PIN()` returns `-1` when not wired/defined
- Every GDO2 code path is guarded by `if (GET_GDO2_PIN() >= 0)`
- All existing users with no GDO2 wiring get exactly the previous behaviour

### Non-ESPHome (MQTT/PlatformIO) Usage

Add to `include/private.h` (see `private.example.h` for template):

```cpp
// Optional: wire CC1101 GDO2 to a free GPIO
#define GDO2 12  // D6 on D1 Mini
```

### ESPHome Usage

Add to your YAML configuration:

```yaml
everblu_meter:
  spi_id: main_bus
  cs_pin: GPIO15
  gdo0_pin: GPIO5
  gdo2_pin: GPIO4    # optional — wire CC1101 GDO2 here
  meter_code: "21-1234567-000"
  time_id: ha_time
```

---

## Verification

Build with PlatformIO (`huzzah` environment):

```bash
pio run --environment huzzah
```

At boot (MQTT mode), with `#define GDO2 12`:
```
✓ GDO2 Pin: GPIO 12 (TX FIFO threshold - hardware-assisted underflow prevention)
```

At boot (ESPHome mode, `dump_config`):
```
[everblu_meter]   GDO2 Pin: configured (HW TX FIFO threshold)
```

Without GDO2 wired:
```
[everblu_meter]   GDO2 Pin: not wired (SPI polling fallback)
```
