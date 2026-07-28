# Release Notes - v3.3.0

A reliability and memory release. No breaking changes to configuration, but ESPHome users will see their `rssi_percentage` values step down slightly on upgrade; see Upgrade notes.

## Highlights: ESP8266 RAM headroom, clearer failure reporting, four memory-safety fixes

- **ESP8266 static RAM drops from 82.5% to 58.7%** of the 81920-byte budget, a 19512-byte saving.
- **Failed reads now name the actual cause** instead of always blaming distance, year and serial.
- **Four memory-safety and scheduling bugs fixed**, including a stack overrun and a stack corruption that ran on every frequency-scan step.
- **The service layer is now covered by host-runnable tests**, which is how three of those four bugs were found.

## ESP8266 memory headroom

Two changes account for the saving, and both are automatic:

- **Log format strings are held in flash, not DRAM.** `TS_PRINTF`, `TS_PRINTLN` and the `LOG_*` macros route through `printf_P(PSTR(...))` and `println(F(...))`. A plain string literal otherwise lands in `.rodata`, which on ESP8266 is DRAM. Output is identical.
- **The WiFi serial monitor is compiled out when it is off.** `WIFI_SERIAL_MONITOR_ENABLED 0` (the default) previously only skipped the calls, so the 8 KB transmit ring buffer, the 1 KB scratch buffer and the `WiFiServer`/`WiFiClient` objects were still linked in. They now cost nothing unless you turn the monitor on, in which case the previous 8904-byte figure returns and static RAM sits at 69.6%.

If you write code against these macros: on ESP8266 they now need a compile-time literal format string, so `TS_PRINTF(somePointer)` will not compile. Use `TS_PRINTF("%s\n", somePointer)`. ESP32 and the host build are unaffected.

## Failure reporting that points at the right remedy

Every failed read used to report `No meter response (asleep/out of range/wrong Year/Serial)`, which sends you off checking distance, year and serial even when the radio never transmitted. Failures are now classified into four cases, each with its own status, error and log wording:

| Case | What it means |
| --- | --- |
| No reply | The meter did not answer: asleep, out of range, or wrong year/serial |
| CRC failure | The meter replied but the frame was corrupted: marginal RF link or frequency offset |
| Invalid fields | The frame passed CRC but carried implausible meter data |
| Not attempted | `METER_CODE` is missing or malformed, so the radio was never keyed |

The classification is sticky across a retry sequence. Retry messages report the symptom of the attempt that just failed; the final message reports the most informative symptom of the whole sequence.

## Memory-safety and scheduling fixes

- **Stack corruption on every frequency-scan step.** `frequency_manager.h` carried a duplicate `struct tmeter_data` behind a guard that never fired, so `frequency_manager.cpp` used a struct missing two fields. Because the meter-read callback returns that struct by value, each scan step wrote roughly 48 bytes past the caller's return slot. Present since v3.2.0.
- **Stack buffer overrun when logging a frame as hex.** A full 124-byte RADIAN frame needs 372 characters and was written into a 256-byte stack array with no wrapping. Long dumps now wrap onto several lines.
- **Crash when publishing a null radio state (ESPHome).** The null was guarded before the text sensor update but dereferenced immediately afterwards.
- **The post-failure cooldown was skipped when a read failed at `millis() == 0`**, letting the scheduler retry immediately in the first millisecond after boot.
- **`MeterHistory::generateHistoryJson()` could report a length longer than the buffer**, publishing truncated, unparseable JSON. It now refuses to truncate.

## Host test coverage

The embedded unit suite now runs on the host, and new native suites cover `MeterReader`, `FrequencyManager`, `MeterHistory`, the CC1101 transmit path and `ESPHomeDataPublisher`. A second PlatformIO environment, `native_esphome`, compiles the shared sources the way the ESPHome external component does, so the publisher code that is `#ifdef`'d out of the MQTT build is tested too. Both environments build with `-fstack-protector-all`, which is what turned the hex-dump overrun from silent corruption into a test failure.

`scripts/run-tests.ps1` runs the same checks as CI locally, with the Python tooling pinned in `scripts/requirements-dev.txt`.

## Upgrade notes

- **ESPHome `rssi_percentage` values change.** The ESPHome publisher had its own copy of the dBm-to-percentage conversion (-120..-50 dBm onto 0-100%) while the shared helper used -120..-40 dBm, so the same reading appeared as one figure in the device log and another in Home Assistant. Both now use the shared helper. Expect a one-off step down in history (-50 dBm now reports 87% instead of 100%) and retune any automation thresholds set against that entity. MQTT values and the LQI percentage are unchanged.
- **If you carried an older `private.h` forward**, wrap `WIFI_SERIAL_MONITOR_ENABLED` in `#ifndef` as `private.example.h` now does, so a `-D` override on the command line does not trigger a macro redefinition.
- **ESP32 builds are pinned to `espressif32@6.12.0`.** Later platform releases ship arduino-esp32 3.x, which drops the implicit `WiFi.h` include that `EspMQTTClient` needs.
- No YAML or MQTT topic changes. `ESPHOME-release/` is regenerated output; use the external component as usual.

## What's Changed

- Host test coverage quick wins by @genestealer in [#139](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/139)
- Compile out the WiFi serial monitor and hold log strings in flash by @genestealer in [#140](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/140)
- Classify read failures and fix frequency-scan stack corruption by @genestealer in [#138](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/138)
- Address PR 138 review: classify unattempted reads, harden the CI variant, align the cooldown by @genestealer in [#141](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/141)

**Full Changelog**: https://github.com/genestealer/everblu-meters-esp8266-improved/compare/v3.2.0...v3.3.0
