# Release Notes - v3.5.0

A diagnostics release: the firmware now says what is wrong with your wiring instead of leaving you to infer it. One breaking requirement, ESPHome 2026.1.0 or later. No YAML key, MQTT topic or pin/wiring changes.

## Highlights: tell a wiring fault from an RF problem

- **New Diagnostic Report button** on both the ESPHome and MQTT builds. One press, one copy-pasteable block, everything a bug report needs.
- **A wrong `gdo0_pin` is now caught.** It used to fail in a way that looked like success, producing "readings" that were only noise.
- **The SPI link self-test runs at boot** and its result appears in the config block, so the log people actually capture contains the evidence.
- **Truncated frames are rejected** instead of being waved through the one integrity check the radio path has.

## The Diagnostic Report button

Diagnosing a broken setup used to depend on the user pressing the right button and keeping the log open long enough. Two reports ([#126](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/126) and a LilyGO T-Embed CC1101 user) had every symptom of a wiring fault, but the logs shared contained no evidence of it: the config block said only `GDO0 Pin: configured`, and the SPI link self-test never ran because it was gated behind the first Home Assistant connection.

The new `diagnostic_report_button` logs a single block covering the configured CS/GDO0/GDO2 pins, a live SPI link self-test, the key CC1101 registers (`PARTNUM`, `VERSION`, `MARCSTATE`, `FREQ2/1/0`, `MDMCFG4/3/2`, `PKTCTRL0`), RSSI/LQI, the current GDO0/GDO2 line levels, the GDO2 fault count, and whether the meter reader ever initialised:

```yaml
everblu_meter:
  diagnostic_report_button:
    name: "Diagnostic Report"
```

It deliberately does not require the meter reader to be initialised, because the usual reason to press it is that the radio never came up.

The standalone MQTT build exposes the same report as a Home Assistant button and on the `<base>/diagnostic_report` topic (payload `report`). The report body lives in the shared CC1101 driver, so both integrations print comparable blocks and a bug report needs only one set of instructions. It is also published retained to `<base>/diagnostic_report_state` and surfaced as a `Diagnostic Report` sensor whose state is the time it was taken and whose `report` attribute holds the text, so it can be read from Home Assistant without a serial cable.

Several details in that block exist because their absence caused real confusion:

- `MARCSTATE` is reported by name. As a bare number, `0x11` reads as a fault when it is usually just a receiver parked with nothing draining the FIFO (`RXFIFO_OVERFLOW`).
- `FREQ2/1/0` is decoded into the actual carrier frequency and printed next to the configured base. The two differ by whatever calibration offset is in effect, which previously hid a ~31 kHz discrepancy that could only be spotted by doing the register arithmetic by hand.
- The GDO0 self-test verdict distinguishes "not run" from "passed", so a report taken before the radio initialised cannot clear GDO0 of a fault that was never checked.

## GDO0 wiring self-test

`cc1101_init()` now checks that GDO0 reads LOW while the radio is IDLE, mirroring the existing GDO2 self-test. `IOCFG0` is sync-word detect and the radio is IDLE at that point, so a correctly wired GDO0 must read LOW; the pin is `INPUT_PULLUP`, so a wrong or unconnected GPIO reads HIGH.

This matters because a mis-assigned `gdo0_pin` previously failed silently in a way that resembled success. Every sync-word wait returned instantly, so the log showed `GDO0 triggered at 0ms` followed by a "received" frame that was really just noise. Nothing else in the driver noticed.

The verdict is kept as state rather than only logged, so a connection that starts failing hours into a run is visible in the report rather than only in a boot message that has long since scrolled away.

## Boot-time SPI self-test and a more useful config block

The SPI link self-test now runs during `setup()` instead of on the first Home Assistant connection, so a stuck MISO or a wrong `cs_pin`/`miso_pin` lands in the boot log:

```
SPI Link Self-Test: PASSED (PARTNUM: 0x00, VERSION: 0x14)
SPI Link Self-Test: FAILED (PARTNUM: 0x0F, VERSION: 0x0F) - the radio is not being read
```

`dump_config()` also reports the actual GPIO numbers now. `GDO0 Pin: configured` becomes `GDO0 Pin: GPIO5`, and the CS pin and RX attenuation are included. Support requests are usually just this block, which could not be checked against a board pinout without the numbers.

## Truncated frames are no longer accepted unchecked

`radian_validate_crc()` used to return "valid" without verifying anything when the length byte claimed more bytes than were decoded. In practice that means a truncated or misaligned capture, so the only integrity gate on the radio path was being bypassed and the downstream parser sanity checks were left to catch the damage.

Such frames are now discarded, and the failure is logged distinctly:

```
Frame truncated: length byte claims 124 bytes, only 120 decoded - the CRC trailer was never received
```

rather than the generic CRC message, which sent people looking at the aerial and the frequency.

## Upgrade notes

- **ESPHome 2026.1.0 or later is required.** The component reports its pin assignments through `GPIOPin::dump_summary(char *, size_t)`, which first shipped in that release. Config validation enforces the floor, so an older install fails during validation with a clear message rather than part-way through the C++ compile. The standalone PlatformIO build is unaffected.
- **No YAML, MQTT topic or wiring migration.** `diagnostic_report_button` is optional and additive; the new MQTT topics are additive.
- **If your meter's frames are consistently truncated, this release may stop readings** that previously came through as intermittently corrupt data. The truncation message above names the cause. Please open an issue with a diagnostic report attached if you hit this.
- **Releases no longer include prebuilt firmware binaries.** They were built with a placeholder `private.h`, so they could never contain a working meter serial or credentials. Build from source as the README describes.

## What's Changed

- Report pin numbers and probe the radio at boot by @genestealer in [#151](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/151)
- Harden the radio and MQTT paths (salvage of stalled PR #103) by @genestealer in [#152](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/152)
- Boot diagnostics, GDO0 self-test, and code-review hardening pass by @genestealer in [#153](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/153)
- Stop building and attaching firmware binaries to releases by @genestealer in [#154](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/154)

**Full Changelog**: https://github.com/genestealer/everblu-meters-esp8266-improved/compare/v3.4.0...v3.5.0
