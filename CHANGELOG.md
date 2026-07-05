# Changelog

All notable changes to this project will be documented in this file.

Releases are created manually by tagging commits with version tags matching `v*.*.*` (e.g., `v2.1.0`). Users should build from source and configure `private.h` with their own meter settings.

## [Unreleased]

### Added

- **Timestamps in MQTT serial log output**: all tagged `[TAG] message` log lines in the standalone (MQTT) firmware now carry a `[HH:MM:SS]` UTC timestamp prefix matching the ESPHome log format. Timestamps are emitted from the moment the firmware starts (showing `[00:00:00]` until NTP syncs, then real UTC wall-clock time). Affects `[STATUS]`, `[MQTT]`, `[TIME]`, `[FREQ]`, `[WIFI]`, `[OTA]`, `[HISTORY]`, `[ERROR]`, `[WARN]`, `[SCHEDULE]`, and all other tagged lines. Plain banner/separator lines (`===...`, `METER READ - START`, etc.) are intentionally left without timestamps.
- **`tuned_frequency` and `frequency_estimate` MQTT sensors**: the standalone (MQTT) build now publishes Home Assistant discovery messages for `Tuned Frequency (MHz)` (unit `MHz`, topic `tuned_frequency`) and `Frequency Estimate` (unit `kHz`, topic `frequency_estimate`), matching the equivalent sensors already present in the ESPHome component.
- **`AUTO_SCAN_ON_FAILURE_ENABLED`** (both builds, opt-in, default `0`): when `MAX_RETRIES` is reached and the firmware enters cooldown, optionally run a narrow ±20 kHz / 1 kHz Deep scan once per failure streak to recalibrate the carrier-frequency offset. Set `#define AUTO_SCAN_ON_FAILURE_ENABLED 1` in `private.h` or `auto_scan_on_failure: true` in ESPHome YAML to enable. Disabled by default to avoid unexpected Wi-Fi/MQTT disruption during the scan (scans block 1–2 minutes).
- **Near-field RF saturation detection**: when a data frame is received but fails CRC and the RSSI is very strong (> −50 dBm), the firmware now logs an explicit `*** NEAR-FIELD SATURATION DETECTED ***` warning explaining that the device is **too close** to the meter (front-end overload), rather than the generic weak-signal message. Both README troubleshooting sections document the symptom and the fix (move 1–2 m away).
- **`scripts/capture-mqtt-log.ps1`**: convenience script that builds, uploads, and captures a timestamped serial monitor log to `temp/mqtt_<date>.log`.
- **`docs/FREQUENCY_CALIBRATION_SYSTEM.md`**: design reference covering the CC1101 bandwidth/FOC register changes, two-phase scan algorithm, FREQEST adaptive tracking loop, CC1101 hardware frequency-resolution boundary, and Fast-scan removal rationale.

### Changed

- **CC1101 RX bandwidth widened 58 kHz → 270 kHz and FOC_LIMIT raised to ±BW/4** (`MDMCFG4 0xF6 → 0x66`, `FOCCFG 0x1D → 0x1E`) in the shared radio driver — affects **both** the MQTT and ESPHome builds. This is the most significant change in this release. The narrow 58 kHz filter with ±7.25 kHz automatic frequency-offset compensation could not tolerate a CC1101 reference-crystal error of more than a few ppm, which is why software frequency scanning was previously needed just to hear the meter. The 270 kHz filter gives the chip **±67.7 kHz of automatic carrier-offset correction (~±156 ppm at 433 MHz)**, so the radio now locks onto the meter at the nominal 433.82 MHz even with a badly off-spec crystal — usually with **no scan required at all**. The RADIAN signal is only ~15 kHz wide, so the extra bandwidth costs ~6.7 dB of noise floor, negligible against the typical >20 dB link margin. Frequency scanning remains available as a fallback for extreme drift or weak signals. See `docs/FREQUENCY_CALIBRATION_SYSTEM.md` for the full derivation.
- **Deep frequency scan algorithm overhauled** (both builds) — two phases replace the previous single-pass sweep:
  - **Phase 1 — window mapping**: scans the configured range in coarse steps, continuing past the first successful decode (`reads_counter > 0`) until `MISS_TOLERANCE` (5) consecutive misses, recording `firstHitFreq` and `lastHitFreq`. Exits as soon as the window closes — no longer scans to the end of range unnecessarily.
  - **Phase 2 — zoom**: always runs. Re-scans `firstHitFreq − step` to `lastHitFreq + step` with 4× finer steps to locate the exact carrier centre. Falls back to the window midpoint if all zoom steps miss (FREQEST adaptive tracking refines further on the next successful read). Single-point windows still trigger the zoom — the coarse hit may be at the band edge.
  - **Zoom step clamped to the CC1101 hardware minimum** (`Fxosc / 2^16 = 26 MHz / 65536 ≈ 397 Hz`). Steps finer than this silently round to the same register value, retesting the same physical frequency; the zoom step is now `max(scanStep × 0.25, 397 Hz)`.
  - `performDeepFrequencyScan()` is now parameterised with optional `scanRangeMHz` (default `0.150`) and `scanStepMHz` (default `0.0025`); existing callers are unaffected by the defaults. The auto-scan-on-failure path uses a narrow `±20 kHz / 1 kHz` call (~41 steps, ~30 s); the manual Deep Scan command and startup scan retain the full `±150 kHz` range.
- **Standalone (MQTT) build now shares the `FrequencyManager` implementation with ESPHome** instead of carrying its own duplicate ([#110](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/110)). `src/main.cpp` previously reimplemented the Deep scan, adaptive FREQEST tracking, and EEPROM/Preferences offset storage (plus a duplicate `FEED_WDT()` watchdog helper) — so every frequency-calibration fix (including the #104 quality guard) had to be written twice. `main.cpp` now registers its `cc1101_init` / `get_meter_data` callbacks with `FrequencyManager` (`src/services/frequency_manager.cpp`) and delegates scanning, adaptive tracking, and persistence to it, keeping only thin MQTT glue (status/offset topic publishing) local. The scan/adaptive/storage logic is now single-sourced across both targets. The ESP8266 EEPROM layout is unchanged (magic `0xABCD` at address 0), so stored calibrations survive the upgrade; on ESP32 the shared store additionally writes a `freq_offset_magic` key, so an offset saved by a pre-refactor ESP32 build is treated as absent once (triggering a one-time re-scan if auto-scan is enabled). Minor behavioural note: remote WiFi-serial log lines are no longer drained per scan step (they arrive after the scan completes) since that MQTT-build-specific pump does not exist in the shared scanner.
- **Deep scan now ranks by decode quality and will not regress a good calibration** (both builds, [#104](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/104)). Previously the scan persisted its chosen frequency unconditionally, ranking candidates by RSSI / first successful decode — so a strong-RSSI frequency tens of kHz off the true carrier (still decoding but with corrupted, CRC-failing bits) could overwrite a well-centred stored offset and degrade subsequent reads. The scan now: (1) performs a **post-lock verification read** at the chosen candidate frequency, measuring demodulation quality via `|FREQEST|` (smallest = best-centred on the carrier); and (2) when a known-good offset is already stored, **only overwrites it if the candidate is strictly better** — it verifies the existing offset too and keeps it unless the candidate decodes with a smaller `|FREQEST|` (or the stored offset no longer decodes at all). A candidate that fails post-lock verification never replaces a working stored offset. First-time calibration (no stored offset) still persists the scan result as before.
- **Deep Scan renamed** from "Scan Frequency" / MQTT topic `freq_scan` to "Deep Frequency Scan" / MQTT topic `deep_scan`. The Home Assistant button name and icon (`mdi:radar`) updated to match.
- **Read retry interval** reduced from 10 seconds to 5 seconds between successive attempts after a failed read.

### Fixed

- **Radio-state hang in `cc1101_rec_mode()`**: the wait loop that spins until the CC1101 reports an RX MARCSTATE (`0x0D`/`0x0E`/`0x0F`) had no timeout. If the radio wedged in a stuck state (e.g. `0x11` RXFIFO_OVERFLOW) it would spin forever while feeding the watchdog — hanging the whole firmware with the activity LED on and no reboot or further logs. The loop is now bounded: on timeout it flushes the RX FIFO (`SFRX`) and re-strobes RX once to recover, and if that still fails it returns so the caller's GDO0 wait times out gracefully.
- **`frequency_estimate` was incorrectly published to `frequency_offset`** in the MQTT `publishMeterReading()` path. The raw CC1101 `FREQEST` register value (the chip's live carrier-offset measurement) is now routed to the correct `frequency_estimate` topic (converted to kHz) and no longer overwrites the persisted offset value published by `publishFrequencyOffset()`.

### Removed

- **Fast frequency scan** (both builds). It was redundant — the two-phase Deep scan does a coarse window-mapping pass followed by a fine zoom, making the old coarse-only Fast scan unnecessary. Removed: `performFastFrequencyScan()`, the `fast_scan` MQTT command and Home Assistant button, and the ESPHome `fast_scan_button` config option. Use the Deep scan (`deep_scan` MQTT topic / `deep_scan_button`) instead.

## [v3.0.1] - 2026-06-26

Non-breaking hardening follow-up to the v3.0.0 GDO2 changes. The default and both opt-out paths are unchanged; correctly-wired setups behave identically to v3.0.0.

### Fixed

- **TX FIFO overflow guard (GDO2 path).** The wake-up FIFO feed loop in `get_meter_data_for_meter()` now verifies real TX free space via `TXBYTES` before writing in GDO2 mode, matching the safety the SPI-polling fallback already had. A miswired / stuck-LOW GDO2 can no longer drive the 64-byte TX FIFO into overflow.

### Added

- **Stuck-GDO2 diagnostics.** When the TX interrogation-frame gate waits the full safety window with GDO2 still HIGH, the driver now logs an explicit "GDO2 still HIGH - check wiring" warning and increments a lifetime counter (`cc1101_get_gdo2_timeout_count()`), so a wiring fault is no longer silently misread as "meter asleep / out of range".
- **Boot-time GDO2 wiring self-test.** `cc1101_init()` performs a one-time check that GDO2 reads LOW with an empty TX FIFO and HIGH once the FIFO is filled past the threshold. A failed toggle logs a warning and bumps the same diagnostic counter, so a miswired GDO2 is flagged at boot instead of only after the first failed read.
- **ESPHome `gdo2_timeouts` diagnostic sensor** (optional) exposing that counter to Home Assistant.

### Changed

- **GDO2 input now uses a pull-up** (matching GDO0), so a disconnected/miswired GDO2 reads HIGH and fails loudly via the stuck-HIGH path instead of floating. ESP8266 has internal pull-ups on every GPIO except GPIO16.
- **ESPHome config validation** now rejects setting both `gdo2_pin:` and `disable_gdo2_fifo_management: true` (previously the opt-out was silently ignored when a pin was also present).

## [v3.0.0] - 2026-06-24

> **⚠️ BREAKING CHANGE** - CC1101 GDO2 hardware-assisted FIFO threshold management is now the **default** mechanism for talking to the radio on **both** the standalone (MQTT) and ESPHome targets. You must wire CC1101 GDO2 to a free GPIO and configure it, **or** explicitly opt out. Existing setups that did not wire GDO2 require migration (see below).
>
> This release also rolls in the previously unreleased v2.4.0 work: the hardware-assisted GDO2 FIFO mechanism (TX + RX) plus reliability, diagnostics, and data-validation improvements. Preserves ESP8266 (Arduino) and ESP32 + ESPHome support.

### Breaking Changes

- **GDO2 is required by default.** The GDO2 hardware FIFO mechanism is now enabled by default instead of being optional:
  - **MQTT / standalone firmware**: `include/private.h` must define either `GDO2 <pin>` (to enable, recommended) or `DISABLE_GDO2_FIFO_MANAGEMENT` (to opt out and keep legacy SPI polling). If neither is defined, the build fails with a clear compile-time `#error` from `src/core/cc1101.cpp` pointing to the README and `docs/GDO2_FIFO_MANAGEMENT.md`.
  - **ESPHome component**: `gdo2_pin:` is now required unless `disable_gdo2_fifo_management: true` is set. If neither is provided, configuration validation fails with a descriptive `cv.Invalid` error explaining both options and linking to the docs.

### Migration Required

- **If you want the new (recommended) behaviour**: wire CC1101 GDO2 to a free GPIO that does not collide with the SPI bus or GDO0, then:
  - MQTT: add `#define GDO2 <pin>` to `include/private.h` (e.g. `#define GDO2 4`).
  - ESPHome: add `gdo2_pin: <GPIO>` to your `everblu_meter:` block (e.g. `GPIO4` on ESP8266, `GPIO27` on ESP32).
- **If you cannot/do not want to wire GDO2**: opt out explicitly to keep the prior SPI-polling behaviour:
  - MQTT: add `#define DISABLE_GDO2_FIFO_MANAGEMENT` to `include/private.h`.
  - ESPHome: add `disable_gdo2_fifo_management: true` to your `everblu_meter:` block.

### Added

- **Hardware-assisted GDO2 FIFO threshold management** (Issues [#83](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/83), [#84](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/84)): the CC1101 driver uses the GDO2 pin as a hardware FIFO threshold signal on both standalone (MQTT) and ESPHome targets, dynamically reconfiguring `IOCFG2` per phase:
  - **TX phase** (`IOCFG2 = 0x02`): GDO2 asserts at the TX FIFO threshold and replaces the stale SPI `TXBYTES` status check and fixed `delay()` in the WUP feeding loop and interrogation-frame gate, proactively preventing `TXFIFO_UNDERFLOW` under ESPHome scheduler load.
  - **RX phase** (`IOCFG2 = 0x01`): GDO2 signals the RX FIFO threshold / end-of-packet, letting the RX drain loop skip unnecessary `RXBYTES` SPI reads while still draining promptly.
- `gdo2_pin` configuration in the ESPHome Python schema (`__init__.py`) and C++ integration, plus the standalone `GDO2` macro; new `cc1101_set_gdo2_pin()` API and `GET_GDO2_PIN()` accessor so both targets share the same logic.
- `disable_gdo2_fifo_management` ESPHome option and `DISABLE_GDO2_FIFO_MANAGEMENT` standalone macro to explicitly opt out of the GDO2 mechanism.
- **Single-day reading schedules**: the reading schedule now accepts a single weekday (e.g. `Monday`) in addition to the `Monday-Friday` / `Monday-Saturday` / `Monday-Sunday` presets, so meters can be read on just one day of the week. (Thanks [@b4dpxl](https://github.com/b4dpxl), [#79](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/79).)
- **ANSI colour support for the standalone Serial / WiFi serial monitor**: log lines are colourised by level (`LOG_*`) and by leading subsystem tag (e.g. `[METER]`, `[FREQ]`) for easier scanning in the VS Code terminal, PlatformIO monitor, or telnet. ESPHome's own logger is unaffected. Disable at build time with `-D EVERBLU_LOG_COLOR=0`. `platformio.ini` now sets `monitor_filters = direct` so the escape sequences render instead of being rewritten to glyphs.
- **Wide frequency scan button for ESPHome radio crystal calibration** ([#96](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/96)): a dedicated `wide_frequency_scan_button` runs the wide-band (±100 kHz) coarse + fine sweep (`performFrequencyScan(true)`). Previously ESPHome only exposed the narrow ±30 kHz refine scan, so a CC1101 whose crystal was off by more than 30 kHz could never lock onto the signal, never saved an offset, and appeared broken/per-meter. Wired through `EverbluMeterTriggerButton` codegen and added to all example YAMLs. The standalone (MQTT) build already performs a first-boot wide scan and is unchanged.
- **Robust ESPHome deploy script** (`scripts/deploy-esphome-to-ha.ps1`, [#97](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/97)): a runnable replacement for the ad-hoc `Remove-Item`/`Copy-Item` deploy notes that clears read-only attributes and uses `robocopy /MIR`, so a partial/aborted copy can no longer leave a broken component on the Home Assistant share.
- Native unit tests covering RADIAN volume and out-of-range time rejection, plus expanded `schedule_manager` schedule-matching tests.
- **Developer tooling**: repo-wide linting/formatting via `.clang-format` (ESPHome code style) for C++, `ruff.toml` for Python, and `.yamllint` for YAML; `ESPHOME/format-component.ps1`/`.sh` helper scripts; an expanded pre-commit configuration; and comprehensive ESPHome CI fixtures (`.ci/esphome/*`) exercising all sensors, options, and code paths (including a legacy GDO2 opt-out config).

### Changed

- **Default maximum read retries lowered from 10 to 5** on both the standalone (MQTT) and ESPHome targets ([#98](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/98)). The count remains user-configurable via `MAX_RETRIES` (`include/private.h`) and `max_retries` (ESPHome, range 1–50); examples, docs, and `ESPHOME-release/` were updated accordingly.
- **Frequency calibration sensors are now global (per-radio)** ([#96](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/96)): `frequency_offset`, `tuned_frequency`, and `frequency_estimate` use static pointers (first-registration wins), so the single radio-wide offset is reflected in one set of Home Assistant entities instead of being duplicated per meter. The frequency offset is a property of the **radio**, so all meters on one CC1101 share the same base frequency.
- **Device-level sensors are now shared across meters** ([#96](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/96)): `CC1101 Connected`, `CC1101 State`, and `Firmware Version` describe the single shared radio/firmware, so their sensor pointers are now static (first-registration wins) and are declared once in the multi-meter example instead of being duplicated per meter.
- **Suppressed verbose per-attempt logging during frequency scans** ([#97](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/97)): frequency scans invoke a full meter-read sequence at every step, which flooded the log with irrelevant `[METER]`/`[CC1101]`/`[RX]` detail. A `g_echo_debug_quiet` flag honoured by `echo_debug()`, plus a non-copyable/non-movable `EchoDebugQuietGuard` RAII helper, wraps the wide/narrow scan functions on both the ESPHome and standalone builds; high-level scan progress (`LOG_*`) remains visible.
- **`ESPHOME/prepare-component-release.ps1` now retries lock-prone file operations** ([#96](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/96)): OneDrive/antivirus can briefly hold handles on `ESPHOME-release/` files; the clear, copy, flatten, include-rewrite, and LF-normalize steps are now wrapped in an `Invoke-WithRetry` helper that waits with an increasing delay before giving up, so the release script and pre-commit hook no longer fail mid-run.
- `FIFOTHR` changed from `0x47` (TX threshold 33 bytes) to `0x49` (TX threshold 25 bytes), guaranteeing ≥ 40 free bytes so the 8-byte WUP buffer and 39-byte interrogation frame can be written after a single GDO2 check. Applied regardless of GDO2 wiring.
- **Non-blocking WiFi serial monitor**: all TCP output now flows through a power-of-two ring buffer drained in `loop()`, so a slow or full TCP socket can no longer stall the main loop. Dropped-byte counts are reported to the client on overrun, the buffer is reset on each new client connection, and the welcome banner now includes the ESP8266 reset reason.
- `include/private.example.h` now enables `#define GDO2 4` by default and documents the `DISABLE_GDO2_FIFO_MANAGEMENT` opt-out.
- All ESPHome examples (`example-water-meter.yaml`, `example-gas-meter-minimal.yaml`, `example-advanced.yaml`, `example-nano-esp32.yaml`, `example-multi-meter.yaml`) now set `gdo2_pin` by default with safe free-GPIO choices and opt-out notes, and include SPI-bus pin warnings (avoiding GPIO12/13/14 on ESP8266).
- **Configuration logging**: the ESPHome `dump_config` and the standalone startup banner report whether GDO0/GDO2 are configured and how FIFO threshold detection is performed. `dump_config` reports the GDO2 fallback state as "disabled (legacy SPI polling fallback)" and the standalone banner reports whether GDO2 is enabled, explicitly disabled via `DISABLE_GDO2_FIFO_MANAGEMENT`, or misconfigured.
- **Meter-read logging clarity** (standalone): each read is now wrapped in `METER READ - START / COMPLETE / FAILED` banner blocks, with the firmware version folded into the START banner (removing the duplicate `[STATUS] Firmware version` line). A successful TX FIFO drain (`MARCSTATE 0x16`) is reported neutrally as the normal end of transmit instead of a false "No response" failure, and the "meter asleep / out of range / wrong Year-Serial" guidance is deferred until both the ACK and data-frame stages actually fail. CRC-failure and frame-timeout messages were reworded to point at RF link quality (antenna/frequency) rather than a code fault, the "First 32 bytes" hex dump is kept on a single line, and a malformed UTC seconds separator (`%02d:%02d/%02d` rendering `16:09/17` instead of `16:09:17`) was corrected at all three time-print sites.
- **WiFi serial live streaming during frequency scans**: the standalone frequency-scan loops now drain the WiFi serial ring buffer between steps, so remote log output streams live during a scan instead of arriving all at once when it completes.
- **Repo-wide formatting/style pass**: C++, Python, and YAML sources reformatted to the new `.clang-format`/`ruff`/`yamllint` rules (accounts for the large mechanical churn in `everblu_meter.cpp`/`.h`, `__init__.py`, and others); no behavioural change.
- **CI workflows** reworked across all nine workflows: added `concurrency` with cancel-in-progress, `paths:` filters so jobs only run on relevant changes, a draft-PR skip guard, and manual `workflow_dispatch`, while retaining continuous per-push PR feedback and `develop` push builds ([#92](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/92), [#94](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/94)).
- `CONTRIBUTING.md`, README, and `ESPHOME/docs/*` updated, including ESPHome Device Builder compatibility notes and a fix for malformed Markdown list structure in the README configuration/advanced sections ([#87](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/87)).
- README and `ESPHOME/README.md` updated: GDO2 documented as required-by-default with opt-out instructions, wiring tables marked accordingly, and ESPHome documentation links surfaced prominently near the top of the main README.
- Standardized file header comments across `src/core/` to the Doxygen `@file`/`@brief` style already used throughout `src/services/` and `src/adapters/`, resolving documentation drift from multiple contributors; added missing headers to `crc_kermit.h`/`.cpp`, `radian_parser.h`/`.cpp`, and `meter_code_parser.h`, and converted plain-comment headers in `cc1101.cpp`, `wifi_serial.h`/`.cpp`, and `version.h`.
- Added the `docs/GDO2_FIFO_MANAGEMENT.md` design document (TX and RX paths).
- Regenerated the `ESPHOME-release/` bundle so it reflects the new GDO2 support and standardized headers.
- Bumped firmware version to `3.0.0`.

### Fixed

- **Frequency offset now persists across power-cycle reboots on ESP8266** ([#96](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/96)): ESPHome assigns preference storage slots by `make_preference()` call order and defaults to RTC memory (wiped on power loss). The previous code created a new preference object inside every `saveFloat`/`loadFloat`, so save and load landed on different slots - the post-save read-back passed but the value was never found again after a reboot. One `ESPPreferenceObject` per key hash is now cached and reused for save/load, with `in_flash=true` so the offset is written to the flash sector and survives a full power cycle. ESP32 (NVS) and the standalone EEPROM path are unaffected.
- **Restored frequency calibration is now confirmed at boot** ([#96](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/96)): `begin()` distinguishes a genuinely persisted offset (even `0.0`) from "nothing stored" using a NaN sentinel, logs an explicit RESTORED-vs-not-found line, and publishes the restored offset and tuned frequency to Home Assistant at boot so persistence can be verified without waiting for the first read.
- **ESPHome `clearKey`/`hasKey` are now consistent and flash-safe** ([#96](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/96)): `clearKey()` previously used a fresh `make_preference<float>(hash)` targeting a different slot/length than `saveFloat`/`loadFloat` and omitting `in_flash`, so it could not reliably invalidate the stored offset. It now reuses the cached flash-backed preference object and writes a zeroed magic so a later `loadFloat` fails its magic check and returns the default. Both `clearKey()` and `hasKey()` guard against a null `global_preferences`, treat a zeroed magic as absent, and sync the cleared slot to flash so the clear survives a reboot.
- **RX frame truncation when GDO2 is wired**: the Stage-2 payload receive loop runs in `PKTCTRL0_INFINITE_LENGTH` mode where the CC1101 never generates an end-of-packet, so GDO2 (`IOCFG2 = 0x01`) only asserted at the 40-byte RX FIFO threshold and the final sub-threshold remainder of each frame was skipped indefinitely. The Stage-2 loop now always polls `RXBYTES` to drain the tail (Stage-1 fixed-length sync loop and TX-side GDO2 logic are unchanged).
- RADIAN data validation now rejects physically impossible meter volumes (> 1 billion litres), guarding against corrupted decode alignment. (Out-of-range time-value rejection already shipped in v2.3.0.)
- `meter_reader` now maintains the active reading state throughout the retry sequence (the "Active Reading" sensor and radio state stay asserted across the whole retry sequence and are cleared only on final success or after max retries, instead of flickering idle between attempts).
- Refresh `MARCSTATE` on a GDO2 underflow break and guard the interrogation-frame write with a FIFO-ready check.
- **Junk sensor values before the first read** ([#69](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/69)): the ESPHome component now publishes its known-at-boot configuration (meter year/serial, schedule, reading time, frequency, firmware version) and sensible idle placeholders (radio `Idle`, status `Ready`, error `None`, `active_reading` false) at the end of `setup()`, so display lambdas and other components no longer read uninitialised sensor state before Home Assistant connects. Numeric sensors with restored history are left untouched so HA-restored values are not overwritten.
- Guard against a null `gmtime()` result in the scheduled-read check and standalone time prints to avoid a potential null dereference.

## [v2.3.0] - 2026-05-15

> **⚠️ BREAKING CHANGE** - This release enforces strict validation of the meter code format. Existing configurations with flexible serial lengths (1–8 digits) **will not validate** without migration. See [Migration Required](#migration-required) below.

### Breaking Changes

- **Meter Code Validation**: The meter code format is now strictly enforced as `YY-SSSSSSS[-NNN]`:
  - `YY`: Exactly 2-digit year
  - `SSSSSSS`: Exactly 7-digit serial number (leading zeros allowed)
  - `NNN`: Optional 3-digit suffix (check digits, ignored if present)
- Serial numbers shorter or longer than 7 digits will now be rejected.
- Error messages updated to clarify "exactly 7 digits" instead of "1 to 8 digits".

### Migration Required

- Update your `METER_CODE` in `include/private.h` to match the strict format `YY-SSSSSSS[-NNN]`.
- Ensure all ESPHome YAML configurations use valid meter codes with exactly 7-digit serials.

### Changed

- Updated `src/core/meter_code_parser.h` to enforce strict 7-digit serial validation.
- Updated ESPHome validator to require `len(serial_str) == 7`.
- Updated error messages and examples to reflect the exact format.
- Updated test cases to validate only 7-digit serials.

### Fixed

- Added new test case to reject short serials (<7 digits).
- Removed outdated references to "1 to 8 digits" in comments and documentation.

## [v2.2.1] - 2026-05-07

### Added

- Added compile-time MQTT option `ENABLE_HA_DISCOVERY` (default `1`) to allow disabling Home Assistant discovery topic publishing (`homeassistant/...`) while keeping raw MQTT telemetry and command topics active (Issue #77).

### Changed

- Updated `include/private.example.h` and README configuration guidance with the new Home Assistant discovery toggle.
- Bumped firmware/component version to `2.2.1`.

## [v2.2.0] - 2026-04-21

> **⚠️ BREAKING CHANGE** - This release contains a breaking ESPHome configuration schema change due to the explicit SPI integration. Existing `everblu_meter` YAML configurations **will not validate** without migration. See [Migration Required](#migration-required) below.

### Breaking Changes

- ESPHome component configuration schema changed.
- Existing ESPHome YAML that relied on implicit CC1101 SPI wiring will no longer validate.
- `everblu_meter` now requires explicit SPI integration fields:
  - top-level `spi:` bus definition
  - `spi_id` under `everblu_meter`
  - `cs_pin` under `everblu_meter`

### Migration Required

- Add a top-level `spi:` block with your board-specific CLK/MOSI/MISO pins.
- Add `spi_id` and `cs_pin` to `everblu_meter`.
- Keep `gdo0_pin` configured under `everblu_meter`.

### Changed

- Bumped firmware/component version to `2.2.0` to reflect the large ESPHome SPI integration and migration impact.
- Consolidated release notes for SPI schema migration, example updates, and ESPHome CC1101 integration hardening.
- Updated ESPHome example configurations to the new SPI schema (`spi:` bus + `spi_id` + `cs_pin`) across water, gas, advanced, and Nano ESP32 examples.
- Added SPI migration guidance in ESPHome README with explicit before/after configuration snippets.
- Clarified ESPHome CC1101 SPI transport behavior and comments to avoid implying the `500kHz` setup call controls ESPHome transfer speed.

### Fixed

- Reduced misleading `[ERROR] TX ABORTED due to TXFIFO_UNDERFLOW` and related `[ERROR]` log messages that appeared whenever a meter did not respond during polling. These are expected conditions (meter asleep, out of range, or wrong Year/Serial configured) and are no longer logged at error level. They now appear with `[METER]`/`[RX]` prefixes and include context explaining likely causes.
- Updated ESPHome `error_sensor` text (visible in Home Assistant) to display actionable messages such as `"No meter response (asleep/out of range/wrong Year/Serial) - retrying"` and `"No meter response after max retries - check distance and meter Year/Serial"` instead of generic failure strings.
- Prevented ESPHome boot state republish from overwriting CC1101 init failures with `status=Ready` and `error=None`.
- Removed unused `cs_pin` storage from ESPHome CC1101 SPI bridge (`cc1101_set_spi_device` now takes only the SPI device pointer).
- Removed unused `CONF_CS_PIN` import in ESPHome component schema module.
- Corrected Nano ESP32 example to include required SPI fields and removed unsupported `consecutive_failures` sensor.

## [v2.1.4] - 2026-03-29

### Changed

- Applied build updates and ESPHome `2026.3` compatibility updates (PR #68 by @davidc).

### Fixed

- Updated ESPHome API usage from `is_connected(true)` to `is_connected_with_state_subscription()` per ESPHome `2026.3.0` API changes.
- Resolved ESPHome compatibility issue tracked in #65.

## [v2.1.3] - 2026-03-12

### Changed

- Bumped firmware version to `2.1.3` for both standalone MQTT firmware and ESPHome release component.
- Updated README release callout to reflect current `V2.1.3` release.

### Fixed

- Documented a reproducible recovery path for ESP32 PlatformIO builds failing with `ModuleNotFoundError: No module named 'intelhex'`.

## [v2.1.2] - 2026-02-13

### Fixed

- CC1101 LQI/FREQEST register reads on buffer overflow (only read when packet completes normally)
- TX underflow error logging (distinct message vs successful completion)
- Comment typo in SFTX flush operation

## [v2.1.1] - 2026-02-13

### Fixed

- Schedule logic bug where "Monday-Sunday" schedule excluded Sunday
- ESPHome component memory leak from raw `new` allocations without destructor
- Schedule constant mismatch in ESPHome Python code (removed "Everyday", "Saturday", "Sunday")
- Duplicate STRINGIFY macro definition with include guards
- ESPHome namespace compliance for `setup_priority` constant

### Added

- Comprehensive unit tests for schedule manager covering all day combinations
- Cross-platform Git hook installation with automatic executable bit setting
- PowerShell executable detection in pre-commit hook with graceful fallback

### Changed

- Updated schedule documentation to use standardized names
- Updated ESPHome testing checklist to reflect current schedule options

### Removed

- `SCHEDULE_EVERYDAY` constant - use `SCHEDULE_MONDAY_SUNDAY` instead
- References to legacy "Everyday", "Saturday", "Sunday" schedule options

## [v2.1.0] - 2026-01-19

**🎉 ESPHome Integration is NOW FULLY WORKING! 🎉**

Major production release with fully functional ESPHome component integration.

### Added

- **ESPHome Integration**: Full production support with 15+ sensors, Home Assistant binary state gating, and button controls
- **Adaptive Frequency Tracking**: Intelligent frequency offset optimization based on successful reads with configurable thresholds
- **Enhanced Sensors**: Read attempts counter, frequency offset monitoring, tuned frequency reporting, and meter status indicators
- **Dual-Mode Release Package**: `ESPHOME-release/` directory for ESPHome distribution, dynamically built from shared source
- **Release Build Scripts**: Cross-platform PowerShell and bash scripts to prepare component distribution
- **Storage Persistence**: Frequency offset persistence with verification (note: offset discovery not persistent between ESPHome reboots)
- **Button Controls**: Manual read triggers, frequency offset reset, and radio reinitialization commands

### Changed

- Updated YAML configuration examples (advanced, minimal, water meter) with new sensors and features
- Enhanced data publishing in both ESPHome and MQTT adapters for new metrics
- Improved logging for frequency adjustments and read attempts
- IntelliSense optimization for ESPHome component development
- Documentation and repository URL updates for release clarity

### Fixed

- ESPHome radio initialization and sensor availability handling
- Log routing in ESPHome to capture all MeterReader logs in UI
- Repository URL references in documentation
- VSCode configuration for ESPHOME-release folder integration

### Known Issues

- CC1101 discovered best frequency not persistent between ESPHome reboots (frequency offset offset discovery mechanism needs refinement)
- All core functionality remains operational despite this limitation

### Testing

- ESPHome integration fully tested on hardware
- Standalone MQTT mode validated on ESP8266
- Dual-mode shared codebase verified with ~95% code reuse

### Dual-Mode Architecture

The `ESPHOME-release/` folder is **dynamically built** from shared source files via `prepare-component-release.ps1` (Windows) or `prepare-component-release.sh` (Unix):

1. **Single Source, Dual Distribution**: Core business logic in `src/` is shared between standalone MQTT and ESPHome modes
2. **Release Preparation**: Build scripts copy entire `src/` tree + ESPHome adapter layer into `ESPHOME-release/everblu_meter/`
3. **Include Path Preservation**: No file modifications-pure structural copy preserves all relative includes
4. **Component Integration**: ESPHome loads all `.cpp`/`.h` files directly from the release package
5. **Maintenance Simplification**: Bug fixes and features in `src/` automatically included in next component distribution

This approach eliminates code duplication while supporting both MQTT discovery and native ESPHome integration seamlessly.

## [v2.0.0] - 2026-01-08

Major architectural refactor with ESPHome integration support.

### Added

- **ESPHome Integration** (⚠️ UNTESTED): Custom component with 15+ sensors, example YAMLs, and documentation
- **Dependency Injection Pattern**: Abstract interfaces (IConfigProvider, ITimeProvider, IDataPublisher) for platform-agnostic operation
- **Dual-Mode Support**: Choose between standalone MQTT or ESPHome with ~95% code sharing
- WiFi Serial Monitor support for remote debugging

### Changed

- Reorganized code structure: `adapters/`, `core/`, `services/` directories
- MeterReader now platform-agnostic using dependency injection
- Improved separation of concerns and maintainability

### Fixed

- Missing includes causing compilation failures
- WiFi Serial Monitor now captures all MeterReader logs
- ESP32 build error with watchdog timer include

### Testing

- ✅ Standalone MQTT mode tested on ESP8266
- ⚠️ ESPHome integration UNTESTED - use with caution

### Breaking Changes

- Source code structure reorganized
- Internal APIs changed due to adapter pattern

⚠️ **Note**: ESPHome component is untested. Standalone MQTT mode validated on hardware.

## [v1.2.0] - 2026-01-07

- Major release version bump.

## [v1.1.7] - 2025-01-07

What's changed since v1.1.6:

### Fixed in v1.1.6

- Automatically append METER_SERIAL to MQTT client ID to prevent conflicts when running multiple devices on the same broker (#35)
- Restored Home Assistant button discovery by removing unsupported per-button availability payloads
- Device now gracefully handles missing CC1101 radio without continuous reboots
- CC1101 State sensor now shows "unavailable" instead of "Idle" when radio is not connected
- WiFi and MQTT functionality maintained even when CC1101 radio is disconnected

## [v1.1.6] - 2026-01-07

What's changed since v1.1.5:

- Add support for gas meters in addition to water meters (Issue #32)
- Add `METER_TYPE` configuration option to select between water (default) or gas meters
- Gas meter readings automatically convert to cubic meters (m³) with configurable divisor
- Add WiFi-based TCP serial monitor for remote debugging via Telnet (disabled by default)
- Add support for multiple board environments in OTA configuration
- Add OTA update support for all boards with intelligent device boot detection
- Add `MAX_RETRIES` configuration option for customizable retry behavior
- Fix auto-alignment to only apply to scheduled reads, not manual MQTT reads (Issue #34)
- Update MQTT discovery and Home Assistant integration for both meter types
- Improve logging with consistent context prefixes for easier debugging
- Rename MQTT entities from `water_meter_*` to `everblu_meter_*` for clarity
- Update documentation with comprehensive setup and configuration guides

## [v1.1.5] - 2025-11-18

What's changed since v1.1.4:

- Address Copilot AI review feedback
- Fix critical VLA stack overflow bug causing data corruption (Issue #20)
- Add `.txt` versions of datasheets
- Document dredzik improvement experiments and validate 4x oversampling strategy
- Add error handling for invalid `reads_counter` values during meter report parsing

## [v1.1.4] - 2025-11-18

What's changed since v1.1.3:

- Fix MQTT discovery JSON construction issues and improve overall code quality
- Fix MQTT discovery for water meter reading sensor
- Improve MQTT topic initialization for meter serial handling
- Add optional MQTT debugging output
- Prefix MQTT topics and entity IDs with `METER_SERIAL` to support multi-meter deployments

## [v1.1.3] - 2025-11-18

What's changed since v1.1.2:

- Enhance RADIAN CRC validation to handle frame length discrepancies
- Add CRC-16/KERMIT verification for RADIAN frames
- Add CRC validation support for RADIAN protocol frames in the CC1101 interface
- Improve error logging around CRC failures

## [v1.1.2] - 2025-11-18

What's changed since v1.1.1:

- Implement automatic wide-frequency scan control on first boot
- Add functions to update and validate reading schedule times using local and UTC time
- Fix multiple definition of `validateConfiguration` (Issue #22)
- Clean up formatting and improve readability of `getch` function
- Add configuration validation helpers with corresponding unit tests
- Add comprehensive Doxygen-style API documentation and `API_DOCUMENTATION.md`

## [v1.1.1] - 2025-11-17

What's changed since v1.1.0:

- Add additional plausibility checks for parsed meter readings (liters) to reject clearly impossible totals before publishing
- Harden historical volume validation to discard inconsistent history blocks while keeping valid primary meter fields
- Make historical attributes JSON generation more robust by using a larger buffer, tracking remaining space, and avoiding malformed payloads on truncation

## [v1.1.0] - 2025-11-17

What's changed since v1.0.1:

- Add first-layer protection against corrupted RADIAN frames using decode-quality and plausibility checks
- Prevent obviously invalid meter data from being published to MQTT
- Surface firmware version (`EVERBLU_FW_VERSION` from `version.h`) as Home Assistant device software version via MQTT discovery
- Add retry handling, frequency scan notifications, and connectivity watchdog
- Add timezone offset support
- Add datasheets to the repository

## [v1.0.1] - 2025-11-01

What's changed since v1.0.0:

- Add WeMos D1 Mini board configuration to release workflow
- Make decoder tolerant to stop-bit errors and fix FALSE macro usage
- Add CC1101/RADIAN debug output control to `private.h` and `cc1101.cpp`
- Rename `config.h` to `private.h` and update related documentation
- Update `.gitignore`

## [v1.0.0] - 2025-10-30

- Initial tagged release

---

Format inspired by Keep a Changelog. For full details, see the Git commit history and GitHub Releases.
