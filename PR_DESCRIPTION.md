# Pull Request

## Title

`feat(cc1101)!: GDO2 hardware-assisted FIFO threshold management — now the default (BREAKING, v3.0.0)`

## Description

### Summary

The CC1101 driver now uses the radio's **GDO2 pin as a hardware FIFO-threshold signal by default** on both the standalone (MQTT) and ESPHome deployment targets. When GDO2 is wired and configured, the driver dynamically reconfigures `IOCFG2` to drive the transmit FIFO during TX and the receive FIFO / end-of-packet during RX, replacing slower and less reliable SPI status polling.

This was previously **optional** (introduced in the unreleased v2.4.0). It is now the **default mechanism**: you must wire GDO2 and configure it, **or** explicitly opt out to keep the legacy SPI-polling behaviour. The SPI-polling fallback is fully preserved behind the opt-out.

This release also bundles the reliability/diagnostics work (non-blocking WiFi serial output), additional RADIAN data validation, clearer meter-read logging, and a documentation/header-standardization pass. The previously unreleased **v2.4.0** changes are rolled into this release. Ships as firmware **v3.0.0**.

> [!WARNING]
> ### ⚠️ Breaking change
> GDO2 is **required by default**. Builds/configs that neither wire GDO2 nor opt out will fail with a clear, actionable error.
>
> - **MQTT / standalone firmware** — `include/private.h` must define **one** of:
>   - `#define GDO2 <pin>` — enable (recommended), or
>   - `#define DISABLE_GDO2_FIFO_MANAGEMENT` — opt out (legacy SPI polling).
>
>   If neither is defined, the build stops with a compile-time `#error` from `src/core/cc1101.cpp` pointing to the README and `docs/GDO2_FIFO_MANAGEMENT.md`.
> - **ESPHome component** — `gdo2_pin:` is now required unless `disable_gdo2_fifo_management: true` is set. If neither is provided, configuration validation fails with a descriptive `cv.Invalid` error that explains both options and links to the docs.

### Migration

**Option A — enable GDO2 (recommended).** Wire CC1101 GDO2 to a free GPIO that does not collide with the SPI bus or GDO0:

| Target | Change |
| ------ | ------ |
| MQTT | Add `#define GDO2 4` (ESP8266) / `#define GDO2 27` (ESP32) to `include/private.h` |
| ESPHome | Add `gdo2_pin: GPIO4` (ESP8266) / `gdo2_pin: GPIO27` (ESP32) to the `everblu_meter:` block |

**Option B — opt out (keep legacy SPI polling).**

| Target | Change |
| ------ | ------ |
| MQTT | Add `#define DISABLE_GDO2_FIFO_MANAGEMENT` to `include/private.h` |
| ESPHome | Add `disable_gdo2_fifo_management: true` to the `everblu_meter:` block |

## What's included

### Default-by-default enforcement (new in v3.0.0)

- **MQTT**: `GET_GDO2_PIN()` in `src/core/cc1101.cpp` now requires either `GDO2` or `DISABLE_GDO2_FIFO_MANAGEMENT`; otherwise a compile-time `#error` is emitted with migration guidance (only in the non-ESPHome branch, so ESPHome builds are unaffected).
- **ESPHome**: new `disable_gdo2_fifo_management` option plus a `_validate_gdo2_required` schema validator that makes `gdo2_pin` mandatory unless the opt-out is set, raising a descriptive `cv.Invalid` error that links to the docs.
- `include/private.example.h` now enables `#define GDO2 4` by default and documents the `DISABLE_GDO2_FIFO_MANAGEMENT` opt-out.
- All `ESPHOME/example-*.yaml` files now set `gdo2_pin` by default (with safe free-GPIO choices and opt-out notes).
- Startup banner / `dump_config` report GDO2 as enabled, explicitly disabled, or misconfigured.

### Part A — TX FIFO threshold via GDO2 (#83)

- `gdo2_pin` parameter in the Python (`__init__.py`) and C++ ESPHome integration, and the standalone build via the `GDO2` macro.
- `cc1101_set_gdo2_pin()`, `GET_GDO2_PIN()` accessor, and an `#ifdef GDO2` fallback so both deployment targets share the same logic.
- `FIFOTHR` changed from `0x47` (TX threshold 33 bytes) to `0x49` (TX threshold 25 bytes), guaranteeing ≥ 40 free bytes so the 8-byte WUP buffer and 39-byte interrogation frame can be written after a single GDO2 check. Applied regardless of GDO2 wiring.
- TX WUP feeding loop and interrogation-frame gate use `digitalRead(GDO2) == LOW` to proactively prevent `TXFIFO_UNDERFLOW` under ESPHome scheduler load, replacing the stale FIFO status check and fixed `delay()`.

### Part B — RX FIFO threshold + EOP and dynamic IOCFG2 (#84)

- The driver dynamically switches `IOCFG2` between TX FIFO threshold mode (`0x02`) for transmit and RX FIFO threshold / end-of-packet mode (`0x01`) for receive.
- GDO2 pin initialisation, conditional read logic, and diagnostics extended to cover the RX path.

### Reliability and diagnostics

- **Non-blocking WiFi serial monitor**: all TCP output now flows through a power-of-two ring buffer drained in `loop()`, so a slow or full TCP socket can no longer stall the main loop. Dropped-byte counts are reported to the client on overrun, the buffer is reset on each new client connection, and the welcome banner now includes the ESP8266 reset reason.
- **RADIAN data validation**: the parser rejects physically impossible meter volumes (> 1 billion litres) and out-of-range time values (instead of clamping), guarding against corrupted decode alignment.
- **Configuration logging**: the ESPHome `dump_config` and the standalone startup banner report whether GDO0/GDO2 are configured and how FIFO threshold detection is performed.

### Meter-read logging clarity

- Fixed a malformed seconds separator in the UTC time prints (`%02d:%02d/%02d` rendered `16:09/17` instead of `16:09:17`) at all three call sites in `main.cpp`.
- Stopped logging a false failure on successful reads: a TX FIFO drain (`MARCSTATE 0x16`) is the normal end-of-frame condition and is now reported neutrally instead of "No response during wake-up". The "meter asleep / out of range / wrong Year-Serial" guidance is deferred until both the ACK and data-frame stages actually fail.
- Kept the "First 32 bytes" hex dump on a single line, avoiding the stray `[D][everblu_meter]` prefix injected mid-line by `show_in_hex_one_line()`.
- Wrapped each read in clear **START / COMPLETE / FAILED** banner blocks on both success and failure exit paths, folding the firmware version into the START banner (removing the duplicate `[STATUS] Firmware version` line).

### Fix — RX frame truncation when GDO2 is wired

The Stage-2 payload receive loop runs in `PKTCTRL0_INFINITE_LENGTH` mode, where the CC1101 never generates an end-of-packet. GDO2 (`IOCFG2 = 0x01`) therefore only asserts at the 40-byte RX FIFO threshold, so the final sub-threshold remainder of each frame left GDO2 LOW indefinitely and was skipped forever — truncating the frame and breaking reception whenever GDO2 was wired. The Stage-2 loop now always polls `RXBYTES` to drain the tail. The Stage-1 sync loop is unaffected (fixed-length `PKTLEN=1`, where EOP does fire) and the TX-side GDO2 logic is unchanged.

### Documentation and housekeeping

- New `docs/GDO2_FIFO_MANAGEMENT.md` design document (TX and RX paths).
- `README.md` updated: GDO2 documented as **required-by-default** with opt-out instructions, wiring tables and quick-references marked accordingly, and **ESPHome documentation links surfaced prominently near the top** of the main README.
- `ESPHOME/README.md` updated with the `gdo2_pin` parameter, the `disable_gdo2_fifo_management` opt-out, and corrected wiring tables (GDO2 marked required-by-default).
- All `ESPHOME/example-*.yaml` files show safe free-GPIO examples for `gdo2_pin` (avoiding the SPI bus pins GPIO12/13/14 on ESP8266).
- `include/private.example.h` updated with an SPI-bus pin warning, a safe GDO2 example pin, and the opt-out macro.
- **Standardized file-header comments** across `src/core/` to the Doxygen `@file`/`@brief` style already used in `src/services/` and `src/adapters/` (added missing headers to `crc_kermit`, `radian_parser`, `meter_code_parser`; converted plain-comment headers in `cc1101.cpp`, `wifi_serial`, `version.h`).
- Added a **pre-commit configuration** for automated code checks.
- Native unit tests covering RADIAN volume and out-of-range time rejection.
- Bumped firmware/component version to **3.0.0** and consolidated the `CHANGELOG.md` into a single v3.0.0 entry (the unreleased v2.4.0 work is folded in).
- Regenerated `ESPHOME-release/` to reflect the new version, headers, validation, and logging changes.

## Compatibility

- **Breaking**: GDO2 is now required by default. Existing setups that did not wire GDO2 must either wire/configure it or explicitly opt out (see Migration above). The SPI-polling path is preserved behind the opt-out, so no functionality is lost.
- CI copies `include/private.example.h` → `include/private.h`, which now defines `GDO2`, so automated ESP8266/ESP32 builds remain green.
- Preserves ESP8266 (Arduino, no `std::` threading) and ESP32 + ESPHome support.

## Notes for reviewers

- `ESPHOME-release/` is generated output — review changes against the source in `ESPHOME/components/everblu_meter/` and regenerate via `ESPHOME/prepare-component-release.ps1`.

## Related issues

- Closes #83 (GDO2 as TX FIFO threshold signal — prevent `TXFIFO_UNDERFLOW`)
- Closes #84 (GDO2 as RX FIFO threshold signal and dynamic `IOCFG2` reconfiguration)

## Testing

- [ ] MQTT build with `#define GDO2` set
- [ ] MQTT build with `#define DISABLE_GDO2_FIFO_MANAGEMENT` set
- [ ] MQTT build with neither set → expect compile-time `#error`
- [ ] ESPHome config with `gdo2_pin:` set
- [ ] ESPHome config with `disable_gdo2_fifo_management: true`
- [ ] ESPHome config with neither → expect descriptive validation error
- [ ] End-to-end meter read on hardware with GDO2 wired (water + gas)
