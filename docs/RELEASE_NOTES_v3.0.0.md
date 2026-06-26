## v3.0.0 — GDO2 hardware FIFO management (now the default)

> [!WARNING]
> **Breaking change.** CC1101 **GDO2 hardware-assisted FIFO threshold management is now enabled by default** on both the standalone (MQTT) and ESPHome targets. You must wire CC1101 GDO2 to a free GPIO and configure it, **or** explicitly opt out. Existing setups that did not wire GDO2 require migration (see below).

This release also rolls in the previously unreleased v2.4.0 work (the GDO2 TX + RX mechanism) plus reliability, diagnostics, calibration, and data-validation improvements. ESP8266 (Arduino) and ESP32 + ESPHome remain supported.

### Migration (required)

**Option A — enable GDO2 (recommended).** Wire GDO2 to a free GPIO that doesn't collide with the SPI bus or GDO0:

| Target | Change |
| ------ | ------ |
| MQTT | Add `#define GDO2 4` (ESP8266) / `#define GDO2 27` (ESP32) to `include/private.h` |
| ESPHome | Add `gdo2_pin: GPIO4` (ESP8266) / `gdo2_pin: GPIO27` (ESP32) to the `everblu_meter:` block |

**Option B — opt out (keep legacy SPI polling).**

| Target | Change |
| ------ | ------ |
| MQTT | Add `#define DISABLE_GDO2_FIFO_MANAGEMENT` to `include/private.h` |
| ESPHome | Add `disable_gdo2_fifo_management: true` to the `everblu_meter:` block |

If you neither wire GDO2 nor opt out, the MQTT firmware fails to compile and the ESPHome config fails validation — both with a clear, actionable message linking to the docs.

### Highlights

- **GDO2 FIFO threshold (TX + RX)** — `IOCFG2` is dynamically reconfigured per phase: TX FIFO threshold (`0x02`) to prevent `TXFIFO_UNDERFLOW` under ESPHome scheduler load, and RX FIFO threshold / end-of-packet (`0x01`) to cut unnecessary SPI reads. (#83, #84)
- **ESPHome radio-crystal calibration** — new **Wide Frequency Scan** button (±100 kHz) so an uncalibrated crystal can finally be tuned; the frequency offset now **persists across power-cycle reboots on ESP8266** and is confirmed at boot; frequency and device-level sensors are now shared per-radio. (#96)
- **Quieter frequency scans** — per-attempt `[METER]`/`[CC1101]`/`[RX]` log spam is suppressed during scans; high-level progress still shows. New robust ESPHome deploy script. (#97)
- **Configurable retries** — default max read retries lowered **10 → 5** (still tunable via `MAX_RETRIES` / `max_retries`). (#98)
- **Reliability & diagnostics** — non-blocking WiFi serial monitor (ring buffer), stricter RADIAN volume/time validation, clearer START/COMPLETE/FAILED meter-read logging, single-day reading schedules, ANSI log colour.
- **Fixes** — RX frame truncation when GDO2 is wired; junk sensor values before the first read (#69); active-reading state held across retries; null-`gmtime()` guards.

### Compatibility

- **Breaking:** GDO2 required by default (opt-out preserves the legacy SPI-polling path — no functionality lost).
- **Behaviour change:** default read retries dropped from 10 to 5; raise `MAX_RETRIES` / `max_retries` if you relied on the old value.
- CI copies `private.example.h` → `private.h` (now defines `GDO2`), so automated builds stay green.

For the complete entry, see [CHANGELOG.md](../CHANGELOG.md).

## What's Changed

* Daily reading by @b4dpxl in https://github.com/genestealer/everblu-meters-esp8266-improved/pull/79
* Fix malformed README list structure in bottom configuration/advanced sections by @genestealer with @Copilot in https://github.com/genestealer/everblu-meters-esp8266-improved/pull/87
* feat(cc1101): hardware TX FIFO threshold via GDO2 to prevent TXFIFO_UNDERFLOW - PART A (#83) by @genestealer in https://github.com/genestealer/everblu-meters-esp8266-improved/pull/88
* feat(cc1101): dynamic GDO2 IOCFG2 reconfiguration for RX FIFO threshold and EOP signalling - PART B (#84) by @genestealer in https://github.com/genestealer/everblu-meters-esp8266-improved/pull/89
* ci: make workflows on-demand to cut Actions usage by @genestealer in https://github.com/genestealer/everblu-meters-esp8266-improved/pull/92
* ci: restore continuous PR feedback and broaden workflow triggers by @genestealer in https://github.com/genestealer/everblu-meters-esp8266-improved/pull/94
* fix(esphome): publish static config + idle states at boot (issue #69) by @genestealer in https://github.com/genestealer/everblu-meters-esp8266-improved/pull/95
* Suppress verbose per-attempt logging during frequency scans by @genestealer in https://github.com/genestealer/everblu-meters-esp8266-improved/pull/97
* Feat/radio crystal calibration by @genestealer in https://github.com/genestealer/everblu-meters-esp8266-improved/pull/96
* Make meter read retries configurable, default 5 by @genestealer in https://github.com/genestealer/everblu-meters-esp8266-improved/pull/98
* feat(cc1101)!: GDO2 hardware-assisted FIFO threshold management - now the default (BREAKING, v3.0.0) by @genestealer in https://github.com/genestealer/everblu-meters-esp8266-improved/pull/91

## New Contributors

* @b4dpxl made their first contribution in https://github.com/genestealer/everblu-meters-esp8266-improved/pull/79

**Full Changelog**: https://github.com/genestealer/everblu-meters-esp8266-improved/compare/v2.3.0...v3.0.0
