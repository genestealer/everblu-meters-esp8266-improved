# Code Review Report

## EverBlu Meters ESP8266/ESP32 Firmware

**Review Date:** 2026-06-26

**Version:** 3.0.1

**Project:** everblu-meters-esp8266-improved

**Reviewer:** GitHub Copilot (Claude Opus 4.8)

**Scope:** `src/` (MQTT standalone + shared `core`/`services`/`adapters`) and `ESPHOME/components/everblu_meter/`. The generated `ESPHOME-release/` tree was excluded as build output.

---

## Executive Summary

**Overall: Strong (mature, well-documented embedded codebase)**

This firmware reads Itron EverBlu Cyble water/gas meters via a CC1101 433 MHz radio on ESP8266/ESP32 and publishes to Home Assistant over MQTT or ESPHome. The code demonstrates solid engineering: consistent watchdog discipline during multi-second RF transactions, deliberate ESP8266 memory-safety choices (static buffers over VLAs), layered validation of untrusted RF input, and a clean adapter pattern that lets one core compile for two deployment targets.

The findings below are predominantly edge-case hardening rather than systemic defects. Three Critical items relate to buffer/integrity safety on the radio path and are small, self-contained fixes. Every Critical item was verified against the actual source; a small number of automated false positives were investigated and discarded (documented at the end).

---

## Methodology

- Read the two largest and most safety-critical files end-to-end: `src/main.cpp` (2342 lines) and `src/core/cc1101.cpp` (1759 lines).
- Traced the untrusted-RF data path: `receive_radian_frame` → `decode_4bitpbit_serial` → `parse_meter_report` / `radian_parser`, validating buffer sizing at each stage.
- Reviewed the ESPHome component (`everblu_meter.cpp/.h`, `__init__.py`) and the shared `MeterReader` orchestrator.
- Swept the remaining `core`, `services`, and `adapters` files for security, performance, quality, and architecture issues.
- Confirmed exact line numbers and reachability for each Critical finding.

---

## 🔴 Critical Issues — Should fix

### 1. Stack buffer overflow in `show_in_hex_formatted` for buffers larger than ~85 bytes

**File:** `src/core/utils.cpp:36-78`

`line_pos` is an `int` that accumulates `snprintf`'s return value, but modes 2 and 3 (`show_in_hex_one_line` / `show_in_hex_one_line_GET`) never flush mid-loop the way modes 0/1 do:

```cpp
char line_buf[256];
int line_pos = 0;
...
line_pos += snprintf(line_buf + line_pos, sizeof(line_buf) - line_pos, "%02X ", buffer[i]);
```

Once `line_pos` exceeds 256, `sizeof(line_buf) - line_pos` is evaluated in `size_t` and **underflows to a huge value**, so `snprintf` writes past the 256-byte stack buffer. Each entry is 3 characters, so any input of `len >= 86` overflows.

**Reachability:** latent today. The two live callers cap length at 32 (`cc1101.cpp:913`, `cc1101.cpp:1903`). The one unbounded caller, `cc1101.cpp:865` (`show_in_hex_one_line(rxBuffer, pktLen)`, `pktLen` up to 100), lives in `cc1101_check_packet_received` → `cc1101_wait_for_packet`, which has **no callers in `src/`** (dead code). So it is a live footgun on a function that processes untrusted RF data, not an actively reachable exploit.

**Suggested fix** — guard each write and flush/reset instead of underflowing:

```cpp
int remaining = (int)sizeof(line_buf) - line_pos;
if (remaining <= 4) {                  // not enough room for "0xXX, "
    line_buf[line_pos] = '\0';
    LOG_D("everblu_meter", "%s", line_buf);
    line_pos = 0;
    remaining = (int)sizeof(line_buf);
}
int n = snprintf(line_buf + line_pos, remaining, "%02X ", buffer[i]);
if (n > 0 && n < remaining) line_pos += n;
```

**Rationale:** keeps `line_pos` in bounds regardless of input length and removes the signed/unsigned underflow. Consider also deleting the unused `cc1101_wait_for_packet` / `cc1101_check_packet_received` pair.

---

### 2. CRC validation accepts truncated / over-long frames

**File:** `src/core/radian_parser.cpp:33-37`

```cpp
if (expected_len > size) {
    // Keep compatibility with frames that advertise a longer length than payload.
    return true;          // accepts the frame WITHOUT validating any CRC
}
```

When the length byte from an untrusted RF frame claims more bytes than were actually decoded, the function returns "valid" without checking the CRC. This is the integrity gate for radio input, so a truncated/misaligned capture passes through. (Over-long length fields are, in practice, usually truncated captures.)

**Suggested fix:**

```cpp
if (expected_len > size) {
    return false;         // truncated / misaligned decode is not trustworthy
}
```

**Rationale:** the downstream `parse_meter_report` sanity checks (plausible-volume ceiling, non-decreasing history, forward-tolerance vs. current reading) limit the blast radius, but the CRC is the correct place to drop a corrupt frame cleanly.

> Note: a related "out-of-bounds read at `crc_offset + 1`" concern is **not** real — that path returns before the CRC read, and when `expected_len <= size` the access stays within bounds.

---

### 3. Unbounded `sprintf` into a fixed buffer

**File:** `src/main.cpp:840`

```cpp
char json[512];
sprintf(json, jsonTemplate, meter_data.volume, meter_data.reads_counter, meter_data.battery_left, meter_data.rssi, iso8601);
```

This is the only `sprintf` in a file that otherwise uses `snprintf` throughout. The practical overflow risk is low (fixed template, small integers), but it is an avoidable unbounded write.

**Suggested fix:**

```cpp
snprintf(json, sizeof(json), jsonTemplate, ...);
```

**Rationale:** consistency with the rest of the file and removal of a latent overflow primitive.

---

## 🟡 Suggestions — Improvements to consider

### 4. `0.0` is overloaded as both a valid offset and "not found"

**Files:** `src/main.cpp:2097`, `src/services/frequency_manager.cpp:38`

`loadFrequencyOffset()` returns `0.0` when nothing is stored, and boot logic treats `storedFrequencyOffset == 0.0f` as "no calibration," triggering a wide scan. A meter that genuinely calibrates to a `0.000` MHz offset would be re-scanned on every boot. Prefer a `NaN` sentinel (the `begin()` path already uses `isnan()`), or return an explicit `bool found`.

### 5. Floating-point loop counters in frequency scans

**Files:** `src/main.cpp:1665`, `src/main.cpp:1753`, `src/main.cpp:1795` (also `frequency_manager.cpp`)

```cpp
for (float freq = scanStart; freq <= scanEnd; freq += scanStep)
```

Accumulated rounding can drop or add a final step (`0.005`, `0.003` are not exact in binary). Iterate with an integer index:

```cpp
int steps = (int)lround((scanEnd - scanStart) / scanStep) + 1;
for (int s = 0; s < steps; s++) { float freq = scanStart + s * scanStep; /* ... */ }
```

### 6. Arduino `String` concatenation on the hot path (heap fragmentation)

**Files:** `src/main.cpp:595-645` (the `mqtt.publish(String(mqttBaseTopic) + "/...")` calls), `src/adapters/implementations/mqtt_data_publisher.cpp:93-103`

`onUpdateData()` builds topics with `String(...) + "/suffix"` and `publishHistory()` builds JSON with `String +=` in a loop. On ESP8266 this fragments the ~40 KB heap over time. The file already standardized on `char topicBuffer[MQTT_TOPIC_BUFFER_SIZE]` elsewhere — apply the same here, and `snprintf` the history array.

### 7. `onUpdateData()` size and `goto`

**File:** `src/main.cpp:640-905`

~300 lines with a `goto skip_history_publish` for buffer-exhaustion handling. The ESPHome side already factored the equivalent flow into `MeterReader` + `MeterHistory::generateHistoryJson`. Extracting a `buildHistoryJson()` helper here would remove the `goto` and the duplicated truncation handling.

### 8. Dead / inconsistent bounds code in the SPI layer

**File:** `src/core/cc1101.cpp:568-592`

In `setMHZ`, `byte freq0; if (freq0 > 255)` can never be true (`uint8_t` wraps at 256), so the carry branch is dead. Also `SPIReadBurstReg` / `SPIWriteBurstReg` take `uint8_t len`, making the `len > MAX_SPI_BURST_SIZE (1024)` guard unreachable while reserving two 1 KB static buffers. Harmless, but tighten the types/comments so intent matches reality.

### 9. `0`-terminated history scan assumes readings are never zero

**Files:** `src/main.cpp:672-690`, `src/services/meter_history.cpp:223-230`

Counting valid months stops at the first `0`. That is reasonable for a cumulative counter, but the invariant ("a genuine reading is never 0") is implicit. A short comment documenting it (or a `history_valid[]` companion) would prevent a future maintainer from being surprised by a freshly installed meter.

### 10. Defensive hardening in adapters (lower confidence — worth a glance)

- `src/adapters/implementations/esphome_config_provider.cpp:15-21`: `strncpy(schedule)` with no `nullptr` guard. The current caller passes `std::string::c_str()` (never null), so it is practically safe; add the guard for robustness.
- `src/adapters/implementations/mqtt_data_publisher.h:83-86`: verify the no-MQTT stub implements every pure-virtual (`publishTunedFrequency`, `publishFrequencyEstimate`) so the standalone-without-library build links.
- `src/services/storage_abstraction.cpp:385-387`: `clearAll()` returns `false` silently on ESPHome; a "factory reset" caller cannot tell it was a no-op. Log a warning or implement per-key clearing.

---

## ✅ Good Practices — What is done well

- **Watchdog discipline:** `FEED_WDT()` (+ `yield()`) is threaded through every long RF loop in `cc1101.cpp:30-42`, with the multi-second atomic-read tradeoff thoroughly documented in `everblu_meter.cpp:16-33` (issue #93). The right call for a single-threaded MCU.
- **Deliberate memory safety on ESP8266:** static buffers instead of VLAs / large stack arrays (`cc1101.cpp:443-470`), with explicit reasoning about the ~4 KB stack and serialized SPI access.
- **Untrusted-input validation:** `decode_4bitpbit_serial` bounds every write to `MAX_DECODED_SIZE`, and `parse_meter_report` runs layered sanity checks (non-decreasing history, spike ceiling, forward-tolerance vs. current volume) before trusting RF data (`cc1101.cpp:1020-1110`).
- **Boundary input validation:** MQTT command handlers whitelist exact commands (`main.cpp:1404-1438`); the Python `validate_meter_code` enforces exact digit counts and the 24-bit serial ceiling (`__init__.py:120-185`).
- **Clean architecture:** the adapter interfaces + `MeterReader` orchestrator (`meter_reader.cpp:200-340`) give a testable, well-separated read/retry/cooldown state machine with edge-triggered scheduling, virtual destructors on all interfaces, and a single shared radio/protocol core for both targets.
- **Operational resilience:** the connectivity watchdog with bounded offline reboot (`main.cpp:2255-2342`), the one-time GDO2 wiring self-test and monotonic stuck-timeout counter, and deferring radio init until Home Assistant connects (`everblu_meter.cpp:300-330`).

---

## Investigated and dismissed (no action needed)

- **"JSON injection" in `buildDiscoveryJson` / MQTT discovery:** not a real vulnerability. Every interpolated field is a developer-defined constant (sensor names, device classes) or a numeric serial — not attacker-controlled input. Escaping would be defense-in-depth only.
- **"Out-of-bounds read in `radian_validate_crc` at `crc_offset + 1`":** false positive. The over-long-length branch returns before the CRC read, and the in-bounds branch guarantees `crc_offset + 1 <= size - 1`.

---

## Recommended priority

1. **Critical #1, #2, #3** — radio-path buffer/integrity safety; small, self-contained edits.
2. **#4–#6** — highest-value robustness/performance follow-ups (calibration sentinel, integer scan loops, heap-fragmentation cleanup).
3. **#7–#10** — readability and defensive hardening as time permits.

---

*Generated 2026-06-26. Line numbers reference the repository state at firmware version 3.0.1.*
