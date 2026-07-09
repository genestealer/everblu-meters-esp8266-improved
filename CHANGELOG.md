# Changelog

All notable changes to this project will be documented in this file.

Releases are created manually by tagging commits with version tags matching `v*.*.*` (e.g., `v2.1.0`). Users should build from source and configure `private.h` with their own meter settings.


## AI Notes For Maintainers And Tools

- Treat release sections as the source of truth for shipped behavior.
- The `Unreleased` section is for in-progress work and may be rewritten before tagging.
- If an item was introduced and later superseded in the same release branch, keep only the final behavior in Added/Changed/Fixed/Removed and record superseded work in the AI metadata block.
- Keep PR coverage explicit per release so branch-only work is auditable against merge history.
- Add new versions below, not above this section.

## [Unreleased]

### AI Metadata

```yaml
release_type: minor
base_branch: main
release_branch: radian-decode-improvements
includes_prs: []
notable_superseded_work:
  - "capture-to-end-of-transmission RX experiment added then reverted: it lingered on RF noise until timeout and broke the ACK->data transition; fixed-length capture restored"
scope_summary:
  - "First working end-to-end RADIAN CRC-16/KERMIT validation on live frames"
  - "Full 124-byte frame decode: recovers the 13th history month, meter real-time clock and meter type/identifier"
  - "New ESPHome Meter Clock and Meter Type sensors, Stop Reading button, best-effort deep-scan cancel"
  - "Offline decoder replay tests driven by raw pre-decode RF captures"
```

### Added

- **Meter real-time clock and type/identifier decode**, with matching ESPHome sensors (`meter_clock_sensor`, `meter_model_sensor`) and MQTT topics plus Home Assistant discovery. The clock is decoded from frame bytes [24-26, 28-30] and the ASCII identifier from [32-42], following the RADIAN reference.
- **13th (most recent) monthly history value**: the frame carries 13 months of history, not 12. The final month was previously truncated by the short capture and is now decoded.
- **Stop Reading button** (ESPHome `stop_reading_button`): cancels the current read/retry sequence and requests best-effort cancellation of an in-progress deep frequency scan (it bails at the next scan step; see [#133](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/133)).
- **Diagnostics under `debug_cc1101`**: a raw pre-decode RX buffer dump and a CRC-boundary scan that reports the true frame length and CRC convention.
- **Offline decoder replay tests**: `extract-meter-fixture.py` now also emits raw pre-decode captures to `test/fixtures/meter_frames/raw_frames.lst`, and the new `test_replay_raw_meter_fixtures` native test replays them through the real decoder (`radian_decode_4bitpbit()` → CRC → parse). This covers the 4x-oversampled bit-recovery path offline, not just the parser. Seeded with three meters read twice each.

### Fixed

- **RADIAN CRC now validates end-to-end on live frames for the first time.** Two stacked defects meant the CRC was never actually checked: the raw capture truncated the frame so the CRC trailer was never received, and `radian_validate_crc()` computed the checksum over the wrong range (it skipped the length byte). The frame is 124 bytes; the CRC-16/KERMIT is computed over bytes [0..121] (including the length byte) with the trailer at [122-123]. Verified against multiple live captures.
- **`extract-meter-fixture.py` CRC check used the wrong convention**: it computed the CRC over bytes [1..], skipping the length byte, so captured fixtures were marked `crc_valid=0`. It now matches the firmware and covers bytes [0..].


## [v3.1.1] - 2026-07-08

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: develop
includes_prs: [128]
notable_superseded_work: []
scope_summary:
  - "CC1101 TX data-rate restoration fix for reliable wake-up bursts on repeat reads"
```

### Fixed

- **Wake-up burst truncated to ~60ms on the second and later reads** ([#127](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/127)): `receive_radian_frame()` switches `MDMCFG4` to the 4x-oversampled 9.6 kbps RX rate, but `get_meter_data_for_meter()` never restored the 2.4 kbps TX rate before transmitting. Only the first read after boot worked (fresh from `cc1101_init()`); every later read clocked the wake-up burst out 4x too fast, draining the TX FIFO and hitting `TXFIFO_UNDERFLOW` (MARCSTATE 0x16) after ~60ms instead of the full ~2s burst. The TX phase now rewrites `MDMCFG4`/`MDMCFG3` to 2.4 kbps on every read.



## [v3.1.0] - 2026-07-07

### AI Metadata

```yaml
release_type: minor
base_branch: main
release_branch: develop
includes_prs: [105, 106, 111, 117, 119, 120, 121, 124]
notable_superseded_work:
  - "Manual wide-scan UX/topic naming work superseded by Deep scan naming and behavior"
  - "Earlier scan-flow iterations replaced by final two-phase Deep scan + optional auto-scan-on-failure behavior"
scope_summary:
  - "Frequency calibration reliability and scan workflow"
  - "MQTT sensor/discovery correctness and telemetry"
  - "Decoder correctness, plausibility validation, and history payload shaping"
  - "CI/tooling improvements including Codecov and developer frame-decoder tooling"
```

### Added

- **`Reset Frequency Offset` MQTT button**: the standalone MQTT build now exposes the same frequency-offset reset action as the ESPHome component, with a Home Assistant button and MQTT topic that clears the stored offset and re-tunes the radio.
- **Reading plausibility guard (history cross-check)**: a reading is now rejected when its implied current-month usage (current volume minus the newest history snapshot) exceeds 100× the largest historical monthly usage. Skipped when history is insufficient (fewer than 2 valid months, flat history, or a volume predating the newest snapshot). Both builds (shared `radian_parser`).
- **Codecov code coverage reporting**: a dedicated `coverage.yml` GitHub Actions workflow runs the native PlatformIO test suite with gcov instrumentation (`--coverage`) and uploads results to Codecov via `gcovr`. Coverage is tracked for the three platform-neutral core files exercised by the native tests — `crc_kermit.cpp`, `radian_parser.cpp`, and `radian_decoder.cpp`. A Codecov badge has been added to the README.
- **Expanded native test coverage** ([#125](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/125)): added edge-case unit tests for `radian_decode_4bitpbit()` (glitch recovery, buffer truncation, framing-error handling), `radian_validate_crc()`, and `radian_parse_primary_data()`, raising patch/project coverage of the shared core.
- **Timestamps in MQTT serial log output**: all tagged `[TAG] message` log lines in the standalone (MQTT) firmware now carry a `[HH:MM:SS]` UTC timestamp prefix matching the ESPHome log format. Timestamps are emitted from the moment the firmware starts (showing `[00:00:00]` until NTP syncs, then real UTC wall-clock time). Affects `[STATUS]`, `[MQTT]`, `[TIME]`, `[FREQ]`, `[WIFI]`, `[OTA]`, `[HISTORY]`, `[ERROR]`, `[WARN]`, `[SCHEDULE]`, and all other tagged lines. Plain banner/separator lines (`===...`, `METER READ - START`, etc.) are intentionally left without timestamps.
- **`tuned_frequency` and `frequency_estimate` MQTT sensors**: the standalone (MQTT) build now publishes Home Assistant discovery messages for `Tuned Frequency (MHz)` (unit `MHz`, topic `tuned_frequency`) and `Frequency Estimate` (unit `kHz`, topic `frequency_estimate`), matching the equivalent sensors already present in the ESPHome component.
- **`AUTO_SCAN_ON_FAILURE_ENABLED`** (both builds, opt-in, default `0`): when `MAX_RETRIES` is reached and the firmware enters cooldown, optionally run a narrow ±20 kHz / 1 kHz Deep scan once per failure streak to recalibrate the carrier-frequency offset. Set `#define AUTO_SCAN_ON_FAILURE_ENABLED 1` in `private.h` or `auto_scan_on_failure: true` in ESPHome YAML to enable. Disabled by default to avoid unexpected Wi-Fi/MQTT disruption during the scan (scans block 1–2 minutes).
- **Near-field RF saturation detection**: when a data frame is received but fails CRC and the RSSI is very strong (> −50 dBm), the firmware now logs an explicit `*** NEAR-FIELD SATURATION DETECTED ***` warning explaining that the device is **too close** to the meter (front-end overload), rather than the generic weak-signal message. Both README troubleshooting sections document the symptom and the fix (move 1–2 m away).
- **`scripts/capture-mqtt-log.ps1`**: convenience script that builds, uploads, and captures a timestamped serial monitor log to `temp/mqtt_<date>.log`.
- **`docs/FREQUENCY_CALIBRATION_SYSTEM.md`**: design reference covering the CC1101 bandwidth/FOC register changes, two-phase scan algorithm, FREQEST adaptive tracking loop, CC1101 hardware frequency-resolution boundary, and Fast-scan removal rationale.
- **RADIAN frame decoder developer tool** ([#119](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/119)): added a local/offline decoder utility at `tools/hex_decoder.cpp` (with helper scripts) for protocol analysis and fixture/debug workflows.
- **ESPHome config validation - ESP32 Arduino framework required**: the `everblu_meter` component now fails fast during `esphome config` with a clear message ("requires ESP32 Arduino framework") when an ESP32 target is not using `framework: type: arduino`, instead of failing later in the C++ build with `fatal error: Arduino.h`. ESP8266 is unaffected.
- **ESPHome config validation - GDO0/GDO2 pin conflict**: configuration is now rejected at validation time when `gdo0_pin` and `gdo2_pin` resolve to the same GPIO (the two CC1101 status outputs must be on different pins).
- **ESPHome config validator test coverage**: added negative `esphome config` fixtures (`.ci/esphome/everblu_meter/test.invalid-*.yaml`) and Python unit tests (`tests/esphome/test_validators.py`, 27 cases) exercising the meter-code, GDO2-required, framework, pin-conflict, and reading-schedule validators. Wired into the ESPHome CI workflow as new `negative-validation` (asserts invalid configs are rejected with the expected error) and `python-unit-tests` jobs.
- **`esphome-release-sync` pre-commit hook**: a new local hook (`.pre-commit-config.yaml`) re-runs `ESPHOME/prepare-component-release.sh` whenever files under `src/`, `ESPHOME/components/everblu_meter/`, or the release script itself change. This keeps the generated `ESPHOME-release/` tree in sync when pre-commit.ci autofixes (`ruff`, `clang-format`) rewrite source files — pre-commit.ci now regenerates and commits the release output automatically, so the "ESPHOME Sync Check" job no longer fails on autofix commits. Uncovered and fixed a pre-existing formatting drift in `ESPHOME-release/everblu_meter/__init__.py` (a missing blank line vs. its source).

### Changed

- **MQTT auto-scan recovery now retries once immediately after a successful re-tune**: when `AUTO_SCAN_ON_FAILURE_ENABLED` finds and stores a new frequency offset after max retries, the standalone build performs one final read attempt before entering the 1-hour cooldown. The extra attempt runs at most once per failure streak and only when the scan changed the offset.
- **Deduplicated the RADIAN 4-bit-per-bit decoder** ([#118](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/118)): `decode_4bitpbit_serial()` in `cc1101.cpp` now delegates to the shared, platform-neutral `radian_decode_4bitpbit()` (single source of truth for firmware and the native `hex_decoder` tool). Added native round-trip test coverage. No behavioural change.
- **CC1101 AGC profile rebalanced** ([#109](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/109)): `AGCCTRL2` changed from `0xC7` (42 dB magnitude target, 3 DVGA steps disabled) to `0x43` (33 dB target, 1 DVGA step disabled). The 9 dB lower target gives the AGC loop headroom to reduce gain for strong near-field signals without degrading sensitivity for weak/distant meters. Affects both the MQTT standalone and ESPHome builds (shared driver `src/core/cc1101.cpp`).
- **`RX_ATTENUATION_DB` option added** (MQTT standalone, `include/private.h`): limits the CC1101 LNA gain for permanently close-mounted installations. Values `0` (default) / `6` / `12` / `18` dB. See `include/private.example.h` for usage.
- **`rx_attenuation` option added** (ESPHome, `everblu_meter:` YAML key): equivalent to `RX_ATTENUATION_DB` for the ESPHome build. Values `0` (default) / `6` / `12` / `18`.
- **`AUTO_SCAN_ENABLED` / `auto_scan` default changed to `false`** (both builds): the startup Deep frequency scan no longer runs automatically on first boot. The CC1101 RX bandwidth was widened to 270 kHz (±67.7 kHz automatic carrier-offset compensation) in this release, which means most installations lock onto the meter at the nominal 433.82 MHz without any scan. Enabling the scan unconditionally caused a 6-minute Wi-Fi/MQTT blackout on every new device without benefit. Set `#define AUTO_SCAN_ENABLED 1` (`private.h`) or `auto_scan: true` (ESPHome YAML) to restore the previous behaviour. The scan remains available at any time via the "Deep Frequency Scan" button.
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
- **History payload shaping for monthly usage** ([#124](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/124)): `monthly_usage` now omits the oldest history month (which has no previous month for delta computation), and the JSON-building path was consolidated to keep history payload generation consistent.
- **Example YAML board configuration**: all five example YAMLs now include both an ESP8266 and ESP32 board configuration block; the inactive one is commented out with a clear `==========` section header so users can switch boards by toggling comments. All ESP32 blocks explicitly set `framework: type: arduino` (required since ESPHome 2026.1.0, whose default ESP32 framework is ESP-IDF — the `everblu_meter` component depends on Arduino headers and fails to compile under ESP-IDF).
- **Example YAML SPI pin guidance**: each example now carries a comment on the `spi:` block noting the alternative board's CLK/MOSI/MISO pins (e.g. ESP8266 `GPIO14/13/12` ↔ ESP32 `GPIO18/23/19`).
- **ESP32 CS pin changed from GPIO5 to GPIO25** in `example-advanced.yaml` and `example-gas-meter-minimal.yaml`: GPIO5 is an ESP32 strapping pin that causes boot warnings; GPIO25 is non-strapping, non-SPI, and output-capable. The `example-gas-meter-minimal.yaml` SPI pins and GDO2 pin were also corrected to their ESP32 equivalents (`GPIO18/23/19` and `GPIO27`), which had been left at ESP8266 values.
- **Pin references standardised to `GPIO` prefix** throughout all example YAMLs: bare integer values (e.g. `gdo0_pin: 4`) have been replaced with the `GPIO` prefix form (`gdo0_pin: GPIO4`) for consistency.
- **`reading_schedule` is now validated at config time and case-insensitive** (ESPHome): the option previously accepted any string and silently fell back to `Monday-Friday` at runtime when unrecognised. It is now validated against the known presets/weekdays (matching the C++ `ScheduleManager::isValidSchedule`) and accepts any letter case, normalising e.g. `monday-friday` -> `Monday-Friday` and `FRIDAY` -> `Friday`. An unknown value now fails `esphome config` with the list of valid options.

### Fixed

- **LQI value and percentage corrected** (both builds). The CC1101 LQI register packs `CRC_OK` in bit 7, so a good frame reported an LQI inflated by 128 (e.g. `243` instead of `115`); the stored value now masks bit 7 (`& 0x7F`). LQI is also a demodulation-*error* metric where a lower value is a better link, so `calculateLQIToPercentage()` now uses the 7-bit range and inverts the mapping (LQI `0` → `100%`). The ESPHome build's separate `ESPHomeDataPublisher::calculateLqiPercentage()` was aligned to match, so both builds now agree.
- **Missing newlines in two CC1101 debug log lines** (`src/core/cc1101.cpp`) caused adjacent log messages to run together; the `rssi/lqi/F_est` and `tmo/free_byte/sts` lines now terminate with `\n`.
- **`F_est` printed as unsigned in CC1101 debug output** (`src/core/cc1101.cpp`): the signed `FREQEST` register logged e.g. `F_est=255` instead of `-1`; now printed signed.
- **Stray `)` in the MQTT connection log line** (`src/main.cpp`): `[MQTT] Connected to MQTT Broker)` → `[MQTT] Connected to MQTT Broker`.
- **ESPHome build fix**: `TS_PRINTLN` / `TS_PRINTF` were defined only in the MQTT branch of `logging.h`, so shared files using them failed to compile for ESPHome. The ESPHome branch now also defines them (routed through `ESP_LOGI`).
- **Radio-state hang in `cc1101_rec_mode()`**: the wait loop that spins until the CC1101 reports an RX MARCSTATE (`0x0D`/`0x0E`/`0x0F`) had no timeout. If the radio wedged in a stuck state (e.g. `0x11` RXFIFO_OVERFLOW) it would spin forever while feeding the watchdog — hanging the whole firmware with the activity LED on and no reboot or further logs. The loop is now bounded: on timeout it flushes the RX FIFO (`SFRX`) and re-strobes RX once to recover, and if that still fails it returns so the caller's GDO0 wait times out gracefully.
- **`frequency_estimate` was incorrectly published to `frequency_offset`** in the MQTT `publishMeterReading()` path. The raw CC1101 `FREQEST` register value (the chip's live carrier-offset measurement) is now routed to the correct `frequency_estimate` topic (converted to kHz) and no longer overwrites the persisted offset value published by `publishFrequencyOffset()`.
- **`tuned_frequency` and `frequency_estimate` MQTT sensors blank in Home Assistant** (standalone build). Discovery messages were published but state topics were never written. `adaptiveFrequencyTracking()` now publishes both after every read; `tuned_frequency` is also seeded at boot from the stored calibration.
- **`AUTO_SCAN_ON_FAILURE_ENABLED` C++ default was `true` instead of `false`** (`src/adapters/implementations/define_config_provider.h`): when the macro was not defined in `private.h`, `isAutoScanOnFailureEnabled()` returned `true`, silently enabling failure-recovery scans even though `private.example.h` documented the default as `0` (disabled). The default is now `false` to match the documented behaviour; users who rely on this feature must explicitly set `#define AUTO_SCAN_ON_FAILURE_ENABLED 1`.
- **Boot-uptime timestamp showed `[boot+0s]` instead of elapsed seconds** (`src/core/logging.h`): `everblu_log_timestamp()` used `time()` for the pre-NTP-sync branch, which returns 0 (Unix epoch) on ESP8266 before the clock is set, so the label was always `[boot+0s]`. It now uses `millis()/1000` (actual seconds since reset). The static buffer was also enlarged from 16 to 20 bytes to prevent truncation.
- **Deep scan quality guard treated a valid 0.0 kHz calibration as "no prior calibration"** (`src/services/frequency_manager.cpp`): the guard used `previousOffset == 0.0f` to decide whether to skip the quality comparison and accept the scan result unconditionally. A device whose crystal is perfectly on-spec (offset genuinely 0.0 kHz) would have matched this check and bypassed the protection. A dedicated `s_hasStoredCalibration` flag (set by `begin()` when the NaN sentinel is not returned, and by `saveFrequencyOffset()` on every successful save) is now used instead, correctly distinguishing "no value ever saved" from "stored value happens to be 0.0".
- **Serial history table month labels were off by one** (`src/services/meter_history.cpp`): `printToSerial()` labelled the oldest history entry as `-13` when `monthCount=13`. The formula `monthCount − i` was incorrect; it now uses `monthCount − 1 − i`, matching the `getMonthLabel()` helper and making the oldest entry correctly display as `-12`.

### Removed

- **`MQTTDataPublisher` class** (`src/adapters/implementations/mqtt_data_publisher.{cpp,h}`). The standalone build publishes via `main.cpp` directly and never used this adapter; only the ESPHome build uses `IDataPublisher`. Removed to eliminate dead code.
- **Fast frequency scan** (both builds). It was redundant — the two-phase Deep scan does a coarse window-mapping pass followed by a fine zoom, making the old coarse-only Fast scan unnecessary. Removed: `performFastFrequencyScan()`, the `fast_scan` MQTT command and Home Assistant button, and the ESPHome `fast_scan_button` config option. Use the Deep scan (`deep_scan` MQTT topic / `deep_scan_button`) instead.

### Pull Requests Included In This Release Branch

- [#105](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/105): standalone manual wide-scan button groundwork; later superseded by the current Deep-scan UX/topic naming reflected above.
- [#106](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/106): frequency-scan logging/UX follow-up captured in the scan-related Changed entries.
- [#111](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/111): MQTT frequency-management refactor to shared `FrequencyManager` implementation.
- [#117](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/117): CC1101 AGC rebalance and attenuation configuration improvements.
- [#119](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/119): RADIAN hex/frame decoder developer tooling additions.
- [#120](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/120): LQI/CRC-bit correctness and related logging/value presentation fixes.
- [#121](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/121): decoder deduplication/test follow-up and review-fix pass.
- [#124](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/124): monthly-usage history payload correction (omit oldest month).

## [v3.0.1] - 2026-06-26

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

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

### AI Metadata

```yaml
release_type: major
base_branch: main
release_branch: historical
includes_prs: [79, 87, 91, 92]
notable_superseded_work: []
scope_summary:
  - "GDO2 FIFO management became default with explicit opt-out path (breaking migration)"
  - "Radio reliability/diagnostics hardening and release-bundle standardization"
  - "CI workflow trigger/cost optimization and documentation rendering cleanup"
  - "Single-day schedule support"
metadata_status: derived_from_main_tag_range_and_pr_descriptions
```

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

### AI Metadata

```yaml
release_type: minor
base_branch: main
release_branch: historical
includes_prs: [74, 75, 76, 78, 81, 82]
notable_superseded_work: []
scope_summary:
  - "Migration from year/serial fields to unified meter_code with stricter validation"
  - "ESPHome multi-meter support and validation/schema hardening"
  - "Fixture-based native regression testing and CI automation for frame replay"
  - "Optional MQTT Home Assistant discovery publishing toggle"
  - "Generated-output policy for ESPHOME-release enforced in docs/repo config"
metadata_status: derived_from_main_tag_range_and_pr_descriptions
```

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

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

### Added

- Added compile-time MQTT option `ENABLE_HA_DISCOVERY` (default `1`) to allow disabling Home Assistant discovery topic publishing (`homeassistant/...`) while keeping raw MQTT telemetry and command topics active (Issue #77).

### Changed

- Updated `include/private.example.h` and README configuration guidance with the new Home Assistant discovery toggle.
- Bumped firmware/component version to `2.2.1`.

## [v2.2.0] - 2026-04-21

### AI Metadata

```yaml
release_type: minor
base_branch: main
release_branch: historical
includes_prs: [57, 72]
notable_superseded_work: []
scope_summary:
  - "ESPHome SPI bus integration with explicit spi_id/cs_pin schema (breaking config migration)"
  - "Broader ESPHome external-component CI matrix and local component build test tooling"
metadata_status: derived_from_main_tag_range_and_pr_descriptions
```

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

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

### Changed

- Applied build updates and ESPHome `2026.3` compatibility updates (PR #68 by @davidc).

### Fixed

- Updated ESPHome API usage from `is_connected(true)` to `is_connected_with_state_subscription()` per ESPHome `2026.3.0` API changes.
- Resolved ESPHome compatibility issue tracked in #65.

## [v2.1.3] - 2026-03-12

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

### Changed

- Bumped firmware version to `2.1.3` for both standalone MQTT firmware and ESPHome release component.
- Updated README release callout to reflect current `V2.1.3` release.

### Fixed

- Documented a reproducible recovery path for ESP32 PlatformIO builds failing with `ModuleNotFoundError: No module named 'intelhex'`.

## [v2.1.2] - 2026-02-13

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

### Fixed

- CC1101 LQI/FREQEST register reads on buffer overflow (only read when packet completes normally)
- TX underflow error logging (distinct message vs successful completion)
- Comment typo in SFTX flush operation

## [v2.1.1] - 2026-02-13

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

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

### AI Metadata

```yaml
release_type: minor
base_branch: main
release_branch: historical
includes_prs: [45]
notable_superseded_work: []
scope_summary:
  - "ESPHome integration moved to production-ready status with hardware validation"
  - "Expanded sensor/control surface and adaptive frequency tracking rollout"
  - "Dual-mode packaging workflow for ESPHOME-release maintained from shared sources"
metadata_status: derived_from_main_tag_range_and_pr_descriptions
```

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

### AI Metadata

```yaml
release_type: major
base_branch: main
release_branch: historical
includes_prs: [37]
notable_superseded_work: []
scope_summary:
  - "Large architecture refactor to adapter/dependency-injection design"
  - "Initial dual-target foundation for standalone MQTT and ESPHome component mode"
  - "ESPHome support introduced but not yet production-hardened in this release"
metadata_status: derived_from_main_tag_range_and_pr_descriptions
```

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

### AI Metadata

```yaml
release_type: minor
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

- Major release version bump.

## [v1.1.7] - 2025-01-07

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

What's changed since v1.1.6:

### Fixed in v1.1.6

- Automatically append METER_SERIAL to MQTT client ID to prevent conflicts when running multiple devices on the same broker (#35)
- Restored Home Assistant button discovery by removing unsupported per-button availability payloads
- Device now gracefully handles missing CC1101 radio without continuous reboots
- CC1101 State sensor now shows "unavailable" instead of "Idle" when radio is not connected
- WiFi and MQTT functionality maintained even when CC1101 radio is disconnected

## [v1.1.6] - 2026-01-07

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

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

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

What's changed since v1.1.4:

- Address Copilot AI review feedback
- Fix critical VLA stack overflow bug causing data corruption (Issue #20)
- Add `.txt` versions of datasheets
- Document dredzik improvement experiments and validate 4x oversampling strategy
- Add error handling for invalid `reads_counter` values during meter report parsing

## [v1.1.4] - 2025-11-18

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

What's changed since v1.1.3:

- Fix MQTT discovery JSON construction issues and improve overall code quality
- Fix MQTT discovery for water meter reading sensor
- Improve MQTT topic initialization for meter serial handling
- Add optional MQTT debugging output
- Prefix MQTT topics and entity IDs with `METER_SERIAL` to support multi-meter deployments

## [v1.1.3] - 2025-11-18

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

What's changed since v1.1.2:

- Enhance RADIAN CRC validation to handle frame length discrepancies
- Add CRC-16/KERMIT verification for RADIAN frames
- Add CRC validation support for RADIAN protocol frames in the CC1101 interface
- Improve error logging around CRC failures

## [v1.1.2] - 2025-11-18

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

What's changed since v1.1.1:

- Implement automatic wide-frequency scan control on first boot
- Add functions to update and validate reading schedule times using local and UTC time
- Fix multiple definition of `validateConfiguration` (Issue #22)
- Clean up formatting and improve readability of `getch` function
- Add configuration validation helpers with corresponding unit tests
- Add comprehensive Doxygen-style API documentation and `API_DOCUMENTATION.md`

## [v1.1.1] - 2025-11-17

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

What's changed since v1.1.0:

- Add additional plausibility checks for parsed meter readings (liters) to reject clearly impossible totals before publishing
- Harden historical volume validation to discard inconsistent history blocks while keeping valid primary meter fields
- Make historical attributes JSON generation more robust by using a larger buffer, tracking remaining space, and avoiding malformed payloads on truncation

## [v1.1.0] - 2025-11-17

### AI Metadata

```yaml
release_type: minor
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

What's changed since v1.0.1:

- Add first-layer protection against corrupted RADIAN frames using decode-quality and plausibility checks
- Prevent obviously invalid meter data from being published to MQTT
- Surface firmware version (`EVERBLU_FW_VERSION` from `version.h`) as Home Assistant device software version via MQTT discovery
- Add retry handling, frequency scan notifications, and connectivity watchdog
- Add timezone offset support
- Add datasheets to the repository

## [v1.0.1] - 2025-11-01

### AI Metadata

```yaml
release_type: patch
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

What's changed since v1.0.0:

- Add WeMos D1 Mini board configuration to release workflow
- Make decoder tolerant to stop-bit errors and fix FALSE macro usage
- Add CC1101/RADIAN debug output control to `private.h` and `cc1101.cpp`
- Rename `config.h` to `private.h` and update related documentation
- Update `.gitignore`

## [v1.0.0] - 2025-10-30

### AI Metadata

```yaml
release_type: major
base_branch: main
release_branch: historical
includes_prs: unknown
notable_superseded_work: []
scope_summary: []
metadata_status: inferred_from_changelog
```

- Initial tagged release

---

Format inspired by Keep a Changelog. For full details, see the Git commit history and GitHub Releases.
