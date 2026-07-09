# Release Notes - v3.2.0

A feature release. No breaking changes: existing configurations keep working, and the new ESPHome sensors and button are opt-in.

## Highlights: new ESPHome sensors and control

This release exposes extra fields that the meter was already transmitting, plus a new control:

- **Meter Clock sensor** (`meter_clock_sensor`): the meter's own real-time clock, decoded from the frame and published as `YYYY-MM-DD HH:MM:SS`. Useful for confirming the meter's internal time and spotting clock drift.
- **Meter Type sensor** (`meter_model_sensor`): the meter's type/identifier string (for example `133290AL02`), decoded from the frame. Handy as a per-meter fingerprint and for inventory.
- **Stop Reading button** (`stop_reading_button`): cancels the current read/retry sequence and requests best-effort cancellation of an in-progress deep frequency scan.

### Adding them to your ESPHome YAML

These entities are optional. Add the ones you want under your `everblu_meter` device:

```yaml
everblu_meter:
  - id: water_meter
    # ... existing meter_code / pins / other sensors ...

    # New control button
    stop_reading_button:
      name: "Stop Reading"

    # New diagnostic text sensors
    meter_clock_sensor:
      name: "Meter Clock"
    meter_model_sensor:
      name: "Meter Type"
```

The MQTT standalone firmware publishes the same clock and type fields with Home Assistant discovery, so both deployment paths get the new data. See the updated example YAMLs in `ESPHOME/` and the sensor list in `ESPHOME/README.md`.

## RADIAN CRC validation (now working end-to-end)

For the first time, the RADIAN CRC-16/KERMIT is validated end-to-end on live frames. Two stacked defects previously meant it was never actually checked: the raw capture truncated the frame (dropping the CRC trailer), and the validation covered the wrong byte range (it skipped the length byte). The frame is 124 bytes; the CRC covers bytes `[0..121]` (including the length byte) with the big-endian trailer at `[122..123]`. Corrupted frames are now genuinely rejected before publishing.

## Full 13-month history

The frame carries 13 months of historical volumes, not 12. The most recent month was previously truncated by the short capture and is now decoded and published.

## Offline decoder testing

Captured raw RF can now be replayed through the real decoder in CI, with no meter hardware and even while the meter is asleep:

- `extract-meter-fixture.py` also emits raw pre-decode captures to `test/fixtures/meter_frames/raw_frames.lst`.
- The new `test_replay_raw_meter_fixtures` native test replays them through `radian_decode_4bitpbit()` then CRC and parse, covering the 4x-oversampled bit-recovery path, not just the parser.
- Seeded with three meters read twice each.

See `docs/METER_CAPTURE_AND_CI_TESTING.md`.

## Upgrade notes

- No migration required. To use the new entities, add the `meter_clock_sensor`, `meter_model_sensor` and `stop_reading_button` keys shown above.
- `ESPHOME-release/` is regenerated output; use the external component as usual.
- Follow-up: [#133](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/133) tracks making the deep frequency scan fully non-blocking so the Stop button can abort it mid-scan.

## What's Changed

- Validate RADIAN CRC, decode meter clock/type, add offline decoder tests by @genestealer in [#134](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/134)

**Full Changelog**: https://github.com/genestealer/everblu-meters-esp8266-improved/compare/v3.1.1...v3.2.0
