# Release Notes - v3.4.0

A reliability and diagnostics release. No breaking changes to configuration, YAML keys or MQTT topics.

## Highlights: trustworthy radio detection, clearer multi-meter scan logging

- **A broken SPI bus can no longer masquerade as a healthy CC1101.** `cc1101_init()` now proves the link works before believing anything else the radio reports.
- **Frequency scan logs now name the meter being interrogated**, so a multi-meter setup with mismatched `frequency:` values is visible in the log instead of silently mistuning.
- **The deep frequency scan runs without blocking the host loop**, so the Stop Reading button can abort a manual scan instead of waiting for it to finish.

## CC1101 SPI link self-test

The previous boot-time check only rejected a `VERSION` register read of `0x00` or `0xFF`. A MISO line stuck at any other constant, floating, or held by another device on a shared bus passed that check, and every later register read - `RSSI`, `LQI`, `MARCSTATE`, the whole RX FIFO - returned the same byte. The firmware logged `Radio found OK` and the fault only surfaced later as an unexplained CRC failure that read like a weak RF link.

`cc1101_init()` now runs a write/read-back self-test over `SYNC1`/`SYNC0` with two complementary bit patterns first. A constant or undriven MISO line cannot follow both, so this also catches a missing device, a wrong `miso_pin`, a board bus-routing multiplexer left in the wrong position, and a second SPI device holding the line.

- Failure is now fatal: `radio_connected` stays `false` instead of reporting a healthy radio, with a log message naming the likely cause.
- A device that answers correctly but inconsistently across repeated reads gets a warning about an unreliable bus rather than a hard failure.
- An unrecognised-but-stable silicon revision (not `0x04` or `0x14`) is now accepted with a warning instead of being rejected outright, since read-back has already proven a real device is present.

This was found via issue [#148](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/148): a LilyGO T-Embed CC1101 Plus Shield (CC1101 and nRF24 sharing one SPI bus, plus a bus-routing multiplexer) returned `0x0F` for every register, identically at 2 m and 4 m from the meter, which the old check accepted as a working radio.

A new `env:native_cc1101` PlatformIO environment links the real driver against a simulated CC1101 register file with injectable bus faults (stuck-at-any-constant, absent device, unstable bus), rather than the usual `FakeRadio` seam used by every other host suite.

## Frequency scans in multi-meter setups

In a setup with several `everblu_meter:` entries sharing one CC1101, *which meter answers* a scan is per-entry, but *which frequency range is swept* is a single value shared by every entry, set by whichever entry's `setup()` ran last. That could leave the narrow `auto_scan_on_failure` window centred well away from the carrier without any indication in the log.

`performFrequencyScan()` and the auto-scan-on-failure warning now log the meter code, gas/water type, and the scan centre alongside that entry's configured `frequency:`, so a mismatch is visible at a glance. `ESPHOME/README.md` gains a "Frequency scans in multi-meter setups" section documenting the per-meter/global split, with an explicit warning that every entry sharing a radio must use the same `frequency:`.

## Non-blocking deep frequency scan

Carried over from the [v3.3.0](RELEASE_NOTES_v3.3.0.md) development cycle: the deep frequency scan used to run as two nested blocking loops, so the ESPHome API was never serviced for the duration of the sweep (up to around two minutes) and a Stop button press sat unparsed until the scan finished on its own. The scan is now a state machine stepped one frequency at a time from the host loop, so the existing cancel check fires at the next step boundary. The standalone MQTT build keeps the previous blocking behaviour via a wrapper that pumps the same state machine to completion.

## Upgrade notes

No migration required. No YAML or MQTT topic changes. `ESPHOME-release/` is regenerated output; use the external component as usual.

If your CC1101 wiring or bus sharing is marginal, this release may surface it: a device that previously reported `radio_connected: true` from a stuck bus will now correctly report `false`. Check the boot log for the self-test failure message, which names the likely cause.

## What's Changed

- Make the deep frequency scan non-blocking so Stop can abort it by @genestealer in [#144](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/144)
- test: close remaining coverage gaps for non-blocking deep scan by @genestealer in [#146](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/146)
- Log which meter a frequency scan targets in multi-meter setups by @genestealer in [#147](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/147)
- Fail radio init when the CC1101 SPI link cannot be trusted by @genestealer in [#150](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/150)

**Full Changelog**: https://github.com/genestealer/everblu-meters-esp8266-improved/compare/v3.3.0...v3.4.0
