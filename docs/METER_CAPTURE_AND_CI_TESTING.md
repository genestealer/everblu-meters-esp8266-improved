# Meter Capture and CI Fixture Testing

This guide lets you capture real meter frames on hardware and replay them in GitHub Actions without RF hardware.

## 1) Enable frame dump logs

Enable CC1101 debug logs in your local firmware config.

**MQTT standalone** - add to `include/private.h`:

```c
#define DEBUG_CC1101 1
```

**ESPHome** - add to your device YAML:

```yaml
everblu_meter:
  - id: water_meter
    debug_cc1101: true
```

Build and run firmware as usual, then capture logs to a file while at least one successful read occurs.

Capture commands:

```powershell
# MQTT standalone
pio device monitor --baud 115200 | Tee-Object -FilePath meter-capture.log

# ESPHome (requires ESPHome CLI)
esphome logs your-device.yaml | Tee-Object -FilePath meter-capture.log
```

The extractor handles both log formats automatically:

- MQTT: `[CC1101] Full hex dump of decoded frame (...)`
- ESPHome: `[I][everblu_meter:123]: [CC1101] Full hex dump of decoded frame (...)`

## 2) Convert logs into test fixtures

Run:

```powershell
python scripts/extract-meter-fixture.py --input meter-capture.log --append --name-prefix home
```

This appends fixtures to:

- test/fixtures/meter_frames/fixtures.lst

Each fixture line format:

```text
fixture_name|decoded_hex|volume|battery|counter|time_start|time_end|history_available|crc_valid
```

### Raw pre-decode captures (decoder coverage)

When the log was captured with `debug_cc1101` enabled, it also contains the raw
oversampled RX buffer printed *before* software decode:

```text
[CC1101] Raw pre-decode RX buffer (748 oversampled bytes):
```

The extractor detects these and also writes a second file:

- test/fixtures/meter_frames/raw_frames.lst

These fixtures are replayed through the real decoder
(`radian_decode_4bitpbit()`), so the decode path itself is covered offline, not
just the parser. This is what lets the 4x-oversampled bit-recovery code be
refactored with confidence while the meter is asleep. The raw file uses the same
field layout, with the second column holding the raw oversampled bytes instead
of the decoded frame:

```text
fixture_name|raw_oversampled_hex|volume|battery|counter|time_start|time_end|history_available|crc_valid
```

## 3) Run replay tests locally

```powershell
pio test -e native -v
```

The replay tests validate each captured frame for:

- CRC result expectation
- Parsed volume
- Parsed battery/counter
- Parsed wake window
- History presence flag

`test_replay_meter_fixtures` starts from decoded bytes; `test_replay_raw_meter_fixtures`
starts from the raw oversampled buffer and runs decode + CRC + parse.

## 4) Automatic GitHub testing

Workflow:

- .github/workflows/meter-fixture-tests.yml

This runs on push, pull_request, and manual dispatch, using:

```bash
pio test -e native -v
```

## Notes

- No meter hardware is required in CI.
- Add more captures over time to protect against parser regressions.
- Keep only non-sensitive data in committed logs/fixtures.
