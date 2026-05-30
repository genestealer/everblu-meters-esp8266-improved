# GDO2 as CC1101 FIFO Threshold Signal

|                        |                                                                                                                                                                                                                                             |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Issue**              | [#83](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/83) (TX FIFO — implemented) · [#84](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/84) (RX FIFO + dynamic reconfiguration — future work) |
| **Integration target** | Both (ESPHome + MQTT)                                                                                                                                                                                                                       |
| **Implemented in**     | `feature/gdo2-fifo-threshold` (Part 1 only)                                                                                                                                                                                                 |
| **Future work branch** | New branch off `main` after this merges (Parts 2 & 3)                                                                                                                                                                                       |

GDO2 was previously configured as async serial data output (`IOCFG2 = 0x0D`) and left physically
unconnected. This document describes how wiring GDO2 to a free MCU GPIO and reconfiguring it as
a FIFO threshold signal improves both TX FIFO feeding (preventing underflows) and RX FIFO draining
(reducing unnecessary SPI traffic and improving ESPHome task scheduling).

> **Status summary**
> - **Part 1 (TX FIFO threshold)** — ✅ implemented in `feature/gdo2-fifo-threshold`
> - **Part 2 (RX FIFO threshold)** — ⏳ future work, tracked in issue [#84](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/84)
> - **Part 3 (dynamic IOCFG2 reconfiguration)** — ⏳ future work, tracked in issue [#84](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/84)

---

## Part 1 — TX FIFO Threshold ✅ Implemented

### Problem

The TX FIFO refill loop in `get_meter_data_for_meter()` is driven by polling the `TXBYTES` status
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

|                          | Current                                           | With GDO2 TX                                        |
| ------------------------ | ------------------------------------------------- | --------------------------------------------------- |
| Refill trigger           | SPI read of `TXBYTES` (stale status byte)         | `digitalRead()` — real-time, ~1 µs                  |
| Refill timing            | Fixed `delay(20)` heuristic                       | Event-driven: fires the moment threshold crosses    |
| Underflow detection      | Reactive: poll `MARCSTATE == 0x16` after the fact | Proactive: refill fires before FIFO empties         |
| Interrogation frame gate | Dedicated SPI poll loop (`TXBYTES ≤ 25`)          | Same `digitalRead(GDO2) == LOW` if FIFOTHR adjusted |

### FIFOTHR adjustment

| `FIFO_THR`           | `FIFOTHR` | TX threshold | Free bytes at de-assert | Covers 39-byte frame? |
| -------------------- | --------- | ------------ | ----------------------- | --------------------- |
| 7 (previous default) | `0x47`    | 33 bytes     | ≥ 32                    | No                    |
| 9 (implemented)      | `0x49`    | 25 bytes     | ≥ 40                    | Yes                   |

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

## Part 2 — RX FIFO Threshold ⏳ Future work

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

| Value  | Behaviour                                                                         |
| ------ | --------------------------------------------------------------------------------- |
| `0x00` | HIGH when RX FIFO ≥ threshold; LOW only when FIFO reaches **0**                   |
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

|                             | Current                                    | With GDO2 RX                          |
| --------------------------- | ------------------------------------------ | ------------------------------------- |
| SPI reads during empty FIFO | Every 2 ms regardless                      | Zero — `yield()` until GDO2 fires     |
| ESPHome task scheduling     | Blocked 2 ms per poll iteration            | Proper `yield()` between batches      |
| Final sub-threshold drain   | Collected on next 2 ms poll after GDO0 LOW | Guaranteed by EOP assertion of `0x01` |
| Overflow protection         | `RXBYTES` bit-7 check after the fact       | Unchanged — still the safety net      |

---

## Part 3 — Dynamic GDO2 Reconfiguration ⏳ Future work

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

| File                                                 | Change                                                                                                                                                        |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/core/cc1101.cpp`                                | New `IOCFG2_TX_FIFO_THR` define; `_gdo2_pin` var + `cc1101_set_gdo2_pin()`; update `cc1101_configureRF_0()`; update TX FIFO feeding loop in `get_meter_data_for_meter()` |
| `src/core/cc1101.h`                                  | Expose `cc1101_set_gdo2_pin()`                                                                                                                                |
| `include/private.example.h`                          | Add `GDO2` with wiring note                                                                                                                                   |
| `src/main.cpp`                                       | Report GDO2 pin status in startup validation                                                                                                                  |
| `ESPHOME/components/everblu_meter/__init__.py`       | Add optional `gdo2_pin` config key                                                                                                                            |
| `ESPHOME/components/everblu_meter/everblu_meter.h`   | `set_gdo2_pin()` setter + `gdo2_pin_` member                                                                                                                  |
| `ESPHOME/components/everblu_meter/everblu_meter.cpp` | Call `cc1101_set_gdo2_pin()` in `apply_radio_context()`; log in `dump_config()`                                                                               |
| `ESPHOME/example-*.yaml`                             | Document optional `gdo2_pin:` parameter                                                                                                                       |
| `ESPHOME-release/`                                   | Regenerated via `prepare-component-release.ps1`                                                                                                               |

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
