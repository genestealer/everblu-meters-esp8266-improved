# Release Notes - v3.6.0

A calibration and scheduling release. Frequency calibration is now per meter, scans stand down instead of sweeping a sleeping meter, and several ways a daily read could be silently skipped are fixed. One breaking change affects ESPHome users with a Home Assistant template that reads the `history` array.

## Highlights

- **Per-meter frequency calibration.** Each `everblu_meter:` entry keeps its own base frequency, saved offset, adaptive tracking, sensors and scan buttons. Multi-meter YAML needs updating; see the upgrade notes.
- **A scan stops when the meter goes to sleep** rather than recording hundreds of frequencies as dead, and leaves the working calibration alone.
- **`Frequency Scan` and `Stop Frequency Scan` on the MQTT build**, and scans step from the main loop so Wi-Fi and MQTT stay up for the duration.
- **Scheduled reads are no longer lost** to a one-second timing window, an unset clock at boot, an in-progress scan, or a configured day of week that was never applied.
- **New opt-in to turn automatic daily reads off** on both targets ([#159](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/159)).
- **Breaking:** the cumulative `history` array is out of the ESPHome history JSON, which fixes the sensor showing as `unknown` ([#67](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/67)).

## Breaking change: the ESPHome history payload

Home Assistant rejects an entity state longer than 255 characters and renders the entity as `unknown`. Thirteen seven-digit cumulative readings put the history document well past that limit, which is the root cause of [#67](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/67).

Before:

```json
{"history": [667441, ..., 792364], "monthly_usage": [16773, ...], "current_month_usage": 7276, "months_available": 13}
```

After:

```json
{"monthly_usage": [16773, ...], "current_month_usage": 7276, "months_available": 13}
```

The `monthly_usage` deltas are small enough to always fit. The sensor also publishes a valid empty document at boot, and on a read that decoded no history, instead of going `unavailable`, so a template parsing the JSON never sees `unknown`. History is published on every successful read, so a read without history clears the sensor rather than leaving the previous payload in place.

The MQTT build publishes history as an attribute, which has no length limit, and still carries the full cumulative series. It is unaffected.

## Per-meter calibration

Calibration was previously one offset shared by every meter on the device, which is wrong on two counts: the drift being corrected is in each meter's crystal, and a scan started for one meter could be reset or cancelled from another meter's buttons.

It was worse than shared, in fact. `FrequencyManager::begin()` ran once per `everblu_meter:` entry against a single global base frequency, so whichever entry initialised last decided the frequency for all of them and a scan always swept around that value rather than the triggering meter's. That was documented as a limitation in v3.4.0 and carried a warning in `example-multi-meter.yaml`. Each meter now keeps its own base frequency, so a per-entry `frequency:` finally means what it says.

Each entry now has its own base frequency, saved offset, adaptive tracking and `frequency_estimate` sensor, plus its own Scan, Deep Scan, Reset and Stop buttons. A read reapplies that meter's tuning before transmitting. Pressing Stop on a meter that did not start the running scan now reports `Scan belongs to meter NN-NNNNNN - use that meter's Stop button` to that meter's Last Error sensor, instead of appearing to do nothing.

The practical consequence for multi-meter users is a YAML change: the calibration entities used to be radio-global and the example told you to declare them on the first meter only. They belong on every meter now. See the upgrade notes below.

## Scans

Scans run in stages on both targets: wide acquisition in nominal 10 kHz jumps, one nominal 2.5 kHz fallback pass if that finds nothing, then both edges of the response bracketed and the whole window sampled at 793 Hz intervals. Candidates are ranked on decode reliability before FREQEST is consulted, and the winner is verified before it is saved. A local recovery scan starts around the saved tuning and widens only if nothing answers.

Two things make a scan cost less than it used to:

- **It stops as soon as the response window closes.** Bracketing walks out from the first response and closes an edge after five consecutive misses rather than sweeping to the end of the range, and the fine sweep only covers the bracket those edges define. A sweep that finds nothing restores the previous tuning and leaves the stored calibration alone, so retrying inside the meter's next wake window costs nothing.
- **A successful scan takes the reading straight away.** A scan only reaches a verified result by decoding frames, so the meter is provably awake at that moment. The reader used to publish the new offset and go idle, leaving a recovery scan to sit out the full `retry_cooldown` on a calibration it had just proven. It now takes one confirmation read, reported as `Confirming new calibration`. It is a single attempt: a miss ends the failure streak rather than starting a fresh retry cycle.

Cancelled or unsuccessful scans restore the previous tuning, a radio fault aborts the scan, first-time calibration requires verification, and stored offsets now cover the full ±150 kHz range. Frequency-word conversion rounds to the nearest register value, and FREQEST is sampled at data-frame sync rather than after decoding and logging.

`debug_cc1101: true` no longer floods a scan log with frame hex dumps. The dumper wrote to the log directly and escaped the suppression the scan sets up for itself; frame dumps were 35% of one reported scan log.

## Turning off automatic readings

Set `DISABLE_SCHEDULED_READINGS` to `1` in `include/private.h`, or `disable_scheduled_readings: true` in the ESPHome YAML, and the daily read is skipped. Manual and on-demand reads still work, so the meter is read only when you ask for it. This matters where the utility's own read counter is checked against their scheduled read count, and for anyone who wants to sample a meter occasionally rather than daily.

## Scheduling fixes

Four separate ways a day's reading could disappear:

- **The configured day of week was ignored on the MQTT build.** `main.cpp` never applied `DEFAULT_READING_SCHEDULE` to the schedule manager, so a weekdays-only setting appeared in the logs and in Home Assistant discovery while the reading day came from the default. ESPHome was unaffected.
- **The read fired only on the exact `:00` second.** A blocking read, a scan or a reconnect spanning that one-second window lost the day's reading. It now fires anywhere inside the scheduled minute, latched to one occurrence per day.
- **The 1970 clock at boot could satisfy a 00:00 schedule** and consume the day's read before NTP had synced. The scheduler now waits for a plausible epoch.
- **A scheduled read during a frequency scan was dropped**, not deferred, and lost until the next day. The occurrence is now remembered and taken once the radio is free, which matters because a staged scan, or the cooldown after a failed read, can easily run past the scheduled minute.

The once-per-day latch is also keyed on the full date rather than the day of year, which restarted at 0 each January and would have suppressed the next year's occurrence of the same day.

## Also in this release

- **NTP sync no longer stalls the MQTT connect callback.** It spun in a `delay()` loop for up to 10 seconds on every reconnect, holding up `mqtt.loop()` and `ArduinoOTA.handle()`.
- **The standalone build stopped re-scanning on every boot.** It inferred "no calibration stored" from an offset of exactly 0.0 kHz, so a device whose crystal is on spec never recognised its own saved calibration and spent about two minutes scanning before every MQTT connection.
- **A very weak signal was reported as "too strong".** The dBm conversion returned `int8_t` and the weakest readings map below -128 dBm, which wrapped to a positive number and satisfied the near-field saturation heuristic.
- **The ESPHome manual-read and deep-scan buttons are guarded like the others**, so a deep scan can no longer be launched on an unconfigured or busy radio. On the MQTT build, `Reset Frequency Offset`, `Deep Frequency Scan` and `Diagnostic Report` are likewise ignored during a scan or a retry sequence.
- **An unrecognised reading schedule now says so.** It used to match no day at all, so the device simply never read and gave no reason.
- **`example-advanced.yaml` compiles again**: the `Status` text sensor was missing the `id` its own automation refers to.
- **The release workflow no longer interpolates its dispatch input into a shell command.**
- **The radio read is now covered by host tests.** The simulated CC1101 models the FIFOs, so a full read - wake-up burst, both receive stages, decode, CRC and parse - replays real captured frames on every CI run, in the default, `debug_cc1101` and `DISABLE_GDO2_FIFO_MANAGEMENT` builds.
- Documentation for the frequency override ([#158](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/158)) and LilyGO T-Embed wiring ([#136](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/136)).

## Upgrade notes

- **Run a frequency scan for each meter after upgrading.** The old shared offset has no meter identity and is not imported. Until you do, each meter starts from its configured base frequency.
- **Multi-meter setups: move the calibration entities onto every meter.** `frequency_offset`, `tuned_frequency`, `frequency_estimate`, `deep_scan_button` and `reset_frequency_button` were radio-global, and `example-multi-meter.yaml` told you to declare them on the first meter only. A YAML carried forward unchanged leaves meters 2 and up calibrating with no entities to show for it. The updated example shows the new layout.
- **The new per-meter `scan_button` is optional and additive**, as are the other per-meter buttons.
- **If you have a Home Assistant template reading `history.history[-1]`**, the newest cumulative snapshot is `volume - current_month_usage`, which is equal by definition. Updated templates are in `ESPHOME/docs/ESPHOME_HOME_ASSISTANT_INTEGRATION.md`.
- **If you only use `monthly_usage`, `current_month_usage` or `months_available`**, no action is needed.
- **MQTT users are unaffected by the history change.** No topic, YAML key or wiring migration in this release; the new MQTT scan topics are additive.

## What's Changed

- Code review 2026-09: P1 fixes, NTP de-blocking, scheduler correctness by @genestealer in [#161](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/161)
- Fix open issues: disable scheduled readings (#159), history JSON (#67), schedule wiring; docs for #158/#136 by @genestealer in [#162](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/162)
- Add per-meter calibration, staged scans, and fix ESPHome history JSON by @genestealer in [#163](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/163)

**Full Changelog**: https://github.com/genestealer/everblu-meters-esp8266-improved/compare/v3.5.0...v3.6.0
