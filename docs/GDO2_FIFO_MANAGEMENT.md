# GDO2 as CC1101 FIFO Threshold Signal

**Implements:** GitHub Issues [#83](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/83) / [#84](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/84)  
**Integration target:** Both (ESPHome + MQTT)  
**Branch:** `feature/gdo2-fifo-threshold`

GDO2 is currently configured as async serial data output (`IOCFG2 = 0x0D`) and left physically
unconnected. This document describes how wiring GDO2 to a free MCU GPIO and reconfiguring it as
a FIFO threshold signal improves both TX FIFO feeding (preventing underflows) and RX FIFO draining
(reducing unnecessary SPI traffic and improving ESPHome task scheduling).

---

## Part 1 — TX FIFO Threshold

### Problem

The TX FIFO refill loop in `cc1101_send_and_listen()` is driven by polling the `TXBYTES` status
register over SPI and a fixed `delay(20)` heuristic:

```cpp
// src/core/cc1101.cpp ~L1567
if (CC1101_status_FIFO_FreeByte <= 10) {
    delay(20);
}
SPIWriteBurstReg(TX_FIFO_ADDR, wupbuffer, 8);
```

Two weaknesses:

1. **Stale FIFO level.** `CC1101_status_FIFO_FreeByte` is extracted from the SPI status byte of
   the *previous* register write — not a real-time read. The actual FIFO level at the moment of
   the write can be significantly different.

2. **Fixed delay is unreliable in ESPHome.** `delay()` and `yield()` in ESPHome service background
   tasks (WiFi stack, API server, OTA, mDNS, web server). These can consume tens to hundreds of
   milliseconds unpredictably, causing the TX FIFO to drain at 2.4 kbps while the MCU is occupied.
   This results in `TXFIFO_UNDERFLOW` (MARCSTATE `0x16`). The interrogation frame write has a
   dedicated TXBYTES poll loop as a partial mitigation, but the WUP feeding loop remains vulnerable.

### Proposed solution

Configure GDO2 as **TX FIFO threshold** (`IOCFG2 = 0x02`): asserts HIGH when TX FIFO ≥ threshold,
de-asserts LOW when TX FIFO drops below threshold. A `digitalRead(gdo2_pin)` replaces the SPI
polling in the TX feeding loop:

```cpp
if (GET_GDO2_PIN() >= 0) {
    if (digitalRead(GET_GDO2_PIN()) == LOW) {
        SPIWriteBurstReg(TX_FIFO_ADDR, wupbuffer, 8); // threshold crossed → refill immediately
    }
} else {
    // fallback: existing CC1101_status_FIFO_FreeByte + delay(20) path
}
```

| | Current | With GDO2 TX |
|---|---|---|
| Refill trigger | SPI read of `TXBYTES` (stale status byte) | `digitalRead()` — real-time, ~1 µs |
| Refill timing | Fixed `delay(20)` heuristic | Event-driven: fires the moment threshold crosses |
| Underflow detection | Reactive: poll `MARCSTATE == 0x16` after the fact | Proactive: refill fires before FIFO empties |
| Interrogation frame gate | Dedicated SPI poll loop (`TXBYTES ≤ 25`) | Same `digitalRead(GDO2) == LOW` if FIFOTHR adjusted |

### FIFOTHR adjustment

| `FIFO_THR` | `FIFOTHR` | TX threshold | Free bytes at de-assert | Covers 39-byte frame? |
|---|---|---|---|---|
| 7 (previous default) | `0x47` | 33 bytes | ≥ 32 | No |
| 9 (implemented) | `0x49` | 25 bytes | ≥ 40 | Yes |

Changing `FIFO_THR` from 7 to 9 shifts the TX threshold from 33 → 25 bytes. When GDO2
de-asserts, the FIFO has fewer than 25 bytes remaining, guaranteeing at least 40 free bytes —
enough to safely write both the 8-byte WUP buffer *and* the 39-byte interrogation frame using
the same GDO2 check, eliminating the separate TXBYTES poll loop entirely.

The RX threshold shifts from 33 → 40 bytes as a side effect. This has no functional impact:
the RX loop is gated by GDO0 (sync word detect) and direct `RXBYTES` polling, not by the FIFO
threshold signal.

> **Conservative option**: keep `FIFO_THR=7` and use GDO2 only for the WUP loop. Keep the
> existing `TXBYTES` poll for the interrogation frame gate. This covers ~95% of the underflow
> risk with no FIFOTHR change.

---

## Part 2 — RX FIFO Threshold

### Current RX loop behaviour

```
cc1101_wait_for_packet(ms)            // outer: delay(1) per iteration, polls GDO0
  └─ cc1101_check_packet_received()
       ├─ if (GDO0 == HIGH)            // sync word detected — packet started
       └─ while (GDO0 == HIGH)
              delay(2)                 // blind wait — may be 20ms+ under ESPHome load
              halRfReadReg(RXBYTES)    // SPI read every iteration regardless of fill level
              SPIReadBurstReg(...)     // drain whatever is there
```

Two inefficiencies:

1. **Every `delay(2)` produces an unconditional SPI read.** When the FIFO has 0–1 bytes,
   `RXBYTES` is still read. At 2.4 kbps that is roughly one SPI transaction per ~0.6 bytes
   received — most reads bring back `0x00` and do nothing.

2. **`delay(2)` in ESPHome is not 2 ms.** It yields to the scheduler, which can service WiFi,
   mDNS, the API server, OTA, web_server — any of which may take 10–80 ms. The inner loop fires
   much less often than intended, and during the *wait* phase the firmware is interrupted every
   2 ms by an unnecessary SPI poll rather than being able to yield cleanly.

### Why RX overflow is less likely than TX underflow

At 2.4 kbps the meter injects exactly 1 byte every 3.33 ms. The 64-byte RX FIFO takes ~213 ms
to fill from empty. Even if `delay(2)` balloons to 20 ms, only ~6 bytes arrive per missed drain.
A typical meter response packet is ≤ 40 bytes — overflow requires missing ~10 consecutive reads in
a row. Overflow happens, but it is a softer constraint than TX underflow, which is why this
enhancement is lower priority.

### IOCFG2 signal choices for RX

| Value | Behaviour |
|---|---|
| `0x00` | HIGH when RX FIFO ≥ threshold; LOW only when FIFO reaches **0** |
| `0x01` | Same as `0x00`, **plus** asserts at end-of-packet even if FIFO is below threshold |

Signal `0x01` is the correct choice. It handles two cases that threshold-only cannot:

- **Packet shorter than threshold** (e.g. a 20-byte response with threshold = 33 bytes): GDO2
  never fires on a threshold crossing, but fires at EOP so the final bytes are still drained.
- **Sub-threshold remainder at EOP**: after the last threshold crossing, a few bytes below
  threshold remain when GDO0 goes LOW. GDO2 fires at EOP to collect them regardless.

Signal `0x00` de-asserts only at FIFO-empty, so once HIGH after threshold it stays HIGH during
draining — useful, but the missing EOP assertion means a short packet could never trigger GDO2
at all. Signal `0x01` guarantees GDO2 fires at least once per received packet.

### Proposed RX loop with GDO2

```cpp
while (digitalRead(GET_GDO0_PIN()) == TRUE) {
    if (GET_GDO2_PIN() >= 0) {
        if (digitalRead(GET_GDO2_PIN()) == LOW) {
            yield();   // below threshold — let ESPHome tasks run, no SPI read needed
            continue;
        }
        // GDO2 HIGH: threshold reached or EOP → drain now
    } else {
        delay(2);      // fallback: existing blind poll
    }
    uint8_t rxbytes_reg = halRfReadReg(RXBYTES_ADDR);
    if (rxbytes_reg & 0x80) { /* overflow */ break; }
    l_nb_byte = rxbytes_reg & RXBYTES_MASK;
    if (l_nb_byte && ...) SPIReadBurstReg(RX_FIFO_ADDR, ...);
}
// Post-loop: GDO0 went LOW; EOP already triggered GDO2 for final drain above
```

The shift is from **"poll every 2 ms"** to **"yield until GDO2 says there is work, then drain
in one burst"**.

| | Current | With GDO2 RX |
|---|---|---|
| SPI reads during empty FIFO | Every 2 ms regardless | Zero — `yield()` until GDO2 fires |
| ESPHome task scheduling | Blocked 2 ms per poll iteration | Proper `yield()` between batches |
| Final sub-threshold drain | Collected on next 2 ms poll after GDO0 LOW | Guaranteed by EOP assertion of `0x01` |
| Overflow protection | `RXBYTES` bit-7 check after the fact | Unchanged — still the safety net |

---

## Part 3 — Dynamic GDO2 Reconfiguration

The firmware already reconfigures `MDMCFG2` and `PKTCTRL0` between TX and RX phases. GDO2 follows
the same pattern — one extra register write per phase transition:

```cpp
// New register defines
#define IOCFG2_TX_FIFO_THR       0x02  // TX FIFO at/above threshold
#define IOCFG2_RX_FIFO_THR_EOP  0x01  // RX FIFO at/above threshold OR end-of-packet

// Before entering TX
if (GET_GDO2_PIN() >= 0)
    halRfWriteReg(IOCFG2, IOCFG2_TX_FIFO_THR);

// Before entering RX (cc1101_rec_mode call sites: ~L1262, ~L1346)
if (GET_GDO2_PIN() >= 0)
    halRfWriteReg(IOCFG2, IOCFG2_RX_FIFO_THR_EOP);
```

---

## Part 4 — Implementation Scope

### Hardware prerequisite (user action)

Wire the CC1101 module's **GDO2** pin to any free MCU GPIO. Avoid SPI bus pins, GDO0, and
UART TX/RX. Declare it as `GDO2` in `private.h` (MQTT build) or `gdo2_pin:` in ESPHome YAML.

### Backward compatibility

`gdo2_pin` defaults to `-1` (not configured). All new code paths are guarded:

```cpp
if (GET_GDO2_PIN() >= 0) {
    // GDO2 fast path
} else {
    // existing SPI polling fallback — no behaviour change
}
```

### Files affected

| File | Change |
|---|---|
| `src/core/cc1101.cpp` | New `IOCFG2_TX_FIFO_THR`/`IOCFG2_RX_FIFO_THR_EOP` defines; `_gdo2_pin` var + `cc1101_set_gdo2_pin()`; update `cc1101_configureRF_0()`; update TX and RX loops |
| `src/core/cc1101.h` | Expose `cc1101_set_gdo2_pin()` |
| `include/private.example.h` | Add `GDO2` with wiring note |
| `src/main.cpp` | Report GDO2 pin status in startup validation |
| `ESPHOME/components/everblu_meter/__init__.py` | Add optional `gdo2_pin` config key |
| `ESPHOME/components/everblu_meter/everblu_meter.h` | `set_gdo2_pin()` setter + `gdo2_pin_` member |
| `ESPHOME/components/everblu_meter/everblu_meter.cpp` | Call `cc1101_set_gdo2_pin()` in `apply_radio_context()`; log in `dump_config()` |
| `ESPHOME/example-*.yaml` | Document optional `gdo2_pin:` parameter |
| `ESPHOME-release/` | Regenerated via `prepare-component-release.ps1` |

### Not in scope

- Interrupt-driven refill via `attachInterrupt` on GDO2. SPI writes from ISR context are not safe
  on ESP8266. Polling within the existing TX/RX loops is the correct approach.
- Temperature sensor mode (GDO0/GDO2 shared with ADC) — irrelevant for this use case.

### Alternatives considered

- Keep `FIFO_THR=7` and gate only the WUP refill on GDO2 (conservative option). Eliminates
  ~95% of underflow risk without touching FIFOTHR. The interrogation frame gate keeps its
  existing `TXBYTES` poll loop.
- Use the existing `TXBYTES` poll loop with a tighter threshold and shorter delay — rejected
  because it still depends on `delay()` which is unreliable under ESPHome scheduler load.


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

| Problem                                                                                                          | Effect                                                                                                                                |
| ---------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| `CC1101_status_FIFO_FreeByte` is the **status byte from the previous SPI transaction**, not a fresh TXBYTES read | Stale data; does not reflect current FIFO level                                                                                       |
| `delay(20)` in ESPHome yields to the ESP-IDF/FreeRTOS scheduler                                                  | WiFi, mDNS, OTA, API tasks can stall it for **10–80 ms** — far exceeding the time to drain a 64-byte FIFO at 2.4 kbps (≈213 ms total) |

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

| `FIFOTHR` value     | TX threshold | Free bytes on de-assert | Notes                          |
| ------------------- | ------------ | ----------------------- | ------------------------------ |
| `0x47` (FIFO_THR=7) | 33 bytes     | 32 bytes                | ❌ Not enough for 39-byte frame |
| `0x49` (FIFO_THR=9) | 25 bytes     | **40 bytes**            | ✅ Fits WUP (8) and frame (39)  |

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

| File                                                 | Change                                                                                                         | Scope                 |
| ---------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- | --------------------- |
| `src/core/cc1101.cpp`                                | IOCFG2 = TX_FIFO_THR; FIFOTHR = 0x49; GDO2 pin variable/setter/macro; init pinMode; TX WUP loop; TX frame wait | Both (MQTT + ESPHome) |
| `src/core/cc1101.h`                                  | `cc1101_set_gdo2_pin()` declaration                                                                            | ESPHome only          |
| `include/private.example.h`                          | Optional `#define GDO2 <pin>`                                                                                  | MQTT/PlatformIO only  |
| `src/main.cpp`                                       | GDO2 pin reporting in startup validation                                                                       | MQTT/PlatformIO only  |
| `ESPHOME/components/everblu_meter/everblu_meter.h`   | `set_gdo2_pin()` setter + `gdo2_pin_` member                                                                   | ESPHome only          |
| `ESPHOME/components/everblu_meter/everblu_meter.cpp` | `apply_radio_context()` + `dump_config()`                                                                      | ESPHome only          |
| `ESPHOME/components/everblu_meter/__init__.py`       | `CONF_GDO2_PIN`, optional schema entry, `to_code()`                                                            | ESPHome only          |
| `ESPHOME/example-*.yaml`                             | Commented `gdo2_pin:` entries                                                                                  | ESPHome examples      |
| `ESPHOME-release/`                                   | Regenerated via `prepare-component-release.ps1`                                                                | Generated output      |

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
