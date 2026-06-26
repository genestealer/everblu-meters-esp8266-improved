# Radio Read Blocking Analysis (Issue #93)

> Investigation into the non-blocking behavior of the radio read path in the
> ESPHome loop (`MeterReader::loop`), and whether the CC1101 hardware packet
> engine could make it non-blocking.
>
> **Tracking issue:** [#93 — Investigate non-blocking behavior of the radio read
> path in the ESPHome loop](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/93)
>
> **Status:** Closed as a documented, inherent limitation (see
> [Conclusion](#conclusion)). No functional defect; readings work.

## Summary

ESPHome asks a component's `loop()`/`update()` to return quickly (the documented
guidance is ~30 ms; recent releases raise the *runtime* "took a long time"
warning threshold to ~2550 ms after the 2026.5.0 main-loop/watchdog overhaul).

A CC1101 EverBlu/Cyble meter interrogation is a multi-second, largely synchronous
RF transaction. When a read is active, `EverbluMeterComponent::loop()` blocks for
~3.3 s. This document records:

1. The **measured** block duration and its breakdown.
2. Why a **cooperative state-machine split** (yielding mid-read) is **not viable**
   for this protocol/hardware.
3. How the **official ESPHome `cc1101` component** stays non-blocking and why that
   approach cannot be reused here.
4. Why the **CC1101 hardware packet engine** cannot decode the RADIAN protocol.
5. The final disposition.

The headline result: the read is an **inherent, bounded, watchdog-safe blocking
transaction**. The watchdog is fed throughout (no reboot), so the only artifacts
are a benign loop-time warning and a brief API stall during the infrequent,
scheduled read.

## Measurement

Instrumentation was added around the `meter_reader_->loop()` call in
`EverbluMeterComponent::loop()` (see [`everblu_meter.cpp`](../ESPHOME/components/everblu_meter/everblu_meter.cpp))
to time the call with `millis()` deltas and log the duration at `DEBUG`.

Captured on ESPHome 2026.6.2 (ESP8266, water meter, `logger` level `DEBUG`):

```
[D][everblu_meter:294]: meter_reader loop blocked for 3315 ms (ESPHome budget 30 ms); expected during an active read
[W][component:513]: everblu_meter took a long time for an operation (3327 ms), max is 2550 ms
```

On the first attempt of a read cycle, the API component was also briefly starved:

```
[W][component:513]: api took a long time for an operation (3272 ms), max is 290 ms
```

### Block breakdown (~3.3 s per read attempt)

| Phase                              | Approx. duration | Notes                                                            |
| ---------------------------------- | ---------------- | --------------------------------------------------------------- |
| Wake-up (WUP) burst TX             | ~2140 ms         | Mandatory ~2 s burst; the meter wakes every ~2 s to listen      |
| ACK frame wait                     | ~190 ms          | 150 ms timeout window + overhead                                |
| Data frame wait                    | ~810 ms          | 700 ms timeout window + overhead                                |
| SPI / decode / parse overhead      | ~175 ms          | Register I/O, `decode_4bitpbit_serial`, CRC, parse              |
| **Total**                          | **~3315 ms**     | One indivisible RF transaction                                  |

### Key observations

- **No watchdog reboot at 3315 ms.** The shared radio code feeds the watchdog and
  `yield()`s throughout (see `FEED_WDT()` in [`cc1101.cpp`](../src/core/cc1101.cpp)),
  so WiFi/API/OTA keep being serviced.
- The **WUP TX alone (~2140 ms)** is *under* the 2550 ms runtime budget. The
  overage comes purely from doing TX + RX + parse in one `loop()` call.

## Why a cooperative split is not viable

The intuitive fix — return from `loop()` after the WUP burst and finish the RX on
the next iteration — would keep each call under budget, but it **cannot be done
safely** on a single-threaded MCU for this protocol. Three hard constraints make
the read indivisible:

1. **The WUP TX must be fed continuously.** The CC1101 TX FIFO is 64 bytes and
   drains in ~186 ms at the burst rate. The feed loop must refill it
   continuously, so `loop()` cannot return mid-burst without underflowing the
   FIFO and truncating the wake-up burst.
2. **The meter replies once, in a fixed, non-retransmitted window immediately
   after TX.** Measured ACK arrival is ~45 ms after the burst ends. Returning to
   the ESPHome loop before entering RX — even for a single slow iteration (e.g.
   the `api took a long time (3272 ms)` event above) — would miss the reply.
   There is no retransmission.
3. **The RX FIFO (64 bytes) must be drained continuously.** The reply is sampled
   at the oversampled rate, so the FIFO fills in ~53 ms. It cannot buffer the
   reply across a yield; the CPU must drain it continuously.

Because reads in this deployment are already marginal (RSSI often −88 to
−104 dBm, meters frequently asleep outside their listening window), introducing
*any* timing fragility into the read path is unacceptable. A cooperative split
would convert a benign loop-time warning into intermittent read failures.

## Comparison: official ESPHome `cc1101` component

The official [ESPHome `cc1101` component](https://esphome.io/components/cc1101/)
([source](https://github.com/esphome/esphome/tree/dev/esphome/components/cc1101))
*is* non-blocking on receive. It is worth understanding exactly why, because it
shows the technique is **not transferable** to EverBlu.

- **RX is event-driven, not polled.** In packet mode it sets `GDO0_CFG = 0x01`
  (asserts on RX-FIFO threshold / end-of-packet) and attaches a rising-edge
  interrupt. The ISR calls `enable_loop_soon_any_context()`; `loop()` begins with
  `this->disable_loop()` and returns immediately unless GDO0 is high. So `loop()`
  only runs when the **hardware** has already framed a complete packet — it then
  does a single ≤64-byte FIFO read and fires the `on_packet` trigger.
- **The hardware packet engine does sync detection, length framing, and CRC-16.**
  No continuous draining is needed, so `loop()` is always fast.
- **Even its transmit blocks (bounded).** `transmit_packet()` writes a ≤64-byte
  FIFO packet, calibrates, strobes TX, then busy-waits
  `wait_for_state_(State::IDLE, 1000)` — up to 1 s.

The takeaways:

1. The non-blocking RX path depends entirely on the **hardware packet engine**
   handling *standard* ≤64-byte packets.
2. A blocking radio operation is **sanctioned** in ESPHome when bounded — even the
   official component blocks up to 1 s on TX.

## Why the CC1101 hardware packet engine cannot decode RADIAN

The official component's non-blocking trick requires the radio to speak the
CC1101's native **synchronous** packet format (preamble → sync word → length →
payload → CRC-16). The EverBlu RADIAN protocol does not. This is a
paradigm mismatch, not a tuning problem.

Evidence from the codebase:

### 1. Asynchronous (UART-style) byte framing — the showstopper

`decode_4bitpbit_serial()` in [`cc1101.cpp`](../src/core/cc1101.cpp) documents that
each meter byte is transmitted as:

- **1 start bit (0) + 8 data bits (LSB first) + 3 stop bits (1 1 1)**

The CC1101 packet engine has **no async-serial mode**. It cannot strip per-byte
start/stop bits, and the byte boundaries do not align to its byte clock. It would
deliver the framing bits mixed into the data, requiring software de-framing
regardless.

### 2. Software bit recovery via 4× oversampling

RX is configured at 9.6 kbps to sample the 2.4 kbps signal
(`receive_radian_frame()` in [`cc1101.cpp`](../src/core/cc1101.cpp)), and
`decode_4bitpbit_serial()` recovers each data bit by counting ~4 samples per bit
(`bit_cnt = (bit_cnt + 2) / 4`). The code comments record that **native-rate (1×)
and 8× hardware recovery were tried and were worse** — the meter's signal defeats
the chip's hardware data-slicer/clock recovery, which is exactly what the packet
engine relies on.

### 3. Custom RADIAN CRC — not CRC-16-IBM

`validate_radian_crc()` in [`cc1101.cpp`](../src/core/cc1101.cpp) calls
`radian_validate_crc()`, a RADIAN-specific CRC computed over the *decoded* bytes.
The CC1101 hardware CRC is fixed CRC-16-IBM (polynomial 0x8005) computed over the
*raw* payload, so `CRC_EN` can never validate a RADIAN frame.

### 4. Variable, non-standard frame structure

The decoded frame carries volume/battery/history fields parsed by
`radian_parse_primary_data()`; there is no standard length byte the engine's
variable-length mode could use.

### Transmit side is the same

`Make_Radian_Master_req()` builds the interrogation frame **and its CRC** in
software, and the 2 s wake-up burst requires continuous/infinite-length streaming
— neither is a single hardware FIFO packet.

### What hardware help we *do* use

The only packet-engine features that apply to RADIAN are already in use:

- **Sync-word detection** (`SYNC1` / `SYNC0`) to find frame start.
- **GDO FIFO-threshold signaling** (GDO0/GDO2) to pace TX feeding and RX draining.

Everything else — framing, bit recovery, CRC — must be software, which is what
makes the read inherently blocking.

## Conclusion

A non-blocking radio read is **not achievable** for the EverBlu/RADIAN protocol on
this hardware:

- A cooperative state-machine split would regress reads (the read is one
  indivisible RF transaction with a fixed, non-retransmitted reply window and FIFOs
  that must be serviced continuously).
- The CC1101 hardware packet engine cannot decode RADIAN (async UART framing,
  software 4× bit recovery, custom CRC, non-standard frame).
- Even ESPHome's own `cc1101` component blocks (bounded) on transmit, confirming
  that a bounded blocking radio operation is acceptable.

**Disposition (approach B): accept and document.** The ~3.3 s block is an inherent,
bounded, watchdog-safe limitation of the protocol. The watchdog is fed throughout
(no reboot), reads occur only in the scheduled window, and the only side effects
are a benign loop-time warning and a brief API stall during the read.

A low-noise `DEBUG` diagnostic remains in `EverbluMeterComponent::loop()` to
surface the (expected, bounded) block duration; see the `LOOP_BLOCK_WARN_MS`
comment in [`everblu_meter.cpp`](../ESPHOME/components/everblu_meter/everblu_meter.cpp).

## References

- Issue: [#93](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/93)
- ESPHome component architecture: <https://developers.esphome.io/architecture/components/>
- ESPHome code standards: <https://developers.esphome.io/contributing/code/>
- ESPHome 2026.5.0 main-loop & watchdog overhaul: <https://esphome.io/changelog/2026.5.0/#main-loop-and-watchdog-architecture-overhaul>
- Official ESPHome CC1101 component (docs): <https://esphome.io/components/cc1101/>
- Official ESPHome CC1101 component (source): <https://github.com/esphome/esphome/tree/dev/esphome/components/cc1101>
- CC1101 datasheet: <https://www.ti.com/lit/ds/symlink/cc1101.pdf>
- Shared radio implementation: [`src/core/cc1101.cpp`](../src/core/cc1101.cpp)
- ESPHome read orchestrator: [`src/services/meter_reader.cpp`](../src/services/meter_reader.cpp)
