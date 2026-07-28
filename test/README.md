# Unit Tests

Unit tests for the EverBlu Meters ESP8266/ESP32 project, using the Unity test
framework. Every suite runs on the host: no board, no radio, no meter needed.

## Suites

### `test_native_meter_fixtures/`

End-to-end replay of real radio captures.

- Raw oversampled CC1101 receive buffers from `test/fixtures/meter_frames/raw_frames.lst`
  are pushed through the RADIAN decoder, then the parser, then checked against
  the expected meter reading.
- Fully decoded 124-byte frames from `test/fixtures/meter_frames/fixtures.lst`
  are parsed and checked field by field.
- Also covers CRC validation, framing errors, glitch tolerance, buffer
  truncation, history bounds and read-failure classification.
- `test_transmit_frame.cpp` covers the other direction: the interrogation frame
  built by `Make_Radian_Master_req()` is pushed back through the receiver and
  checked to recover the original payload, identity and CRC.

To add a fixture from a firmware log:

```bash
python scripts/extract-meter-fixture.py --input meter-capture.log --append --name-prefix capture
```

### `test_embedded_unit/`

Pure logic tests for the platform-neutral parts of the firmware.

- `test_config_validation.cpp` - meter code parsing, schedule string validation
- `test_schedule_manager.cpp` - reading days, UTC/local conversion, clamping, auto-alignment to the meter wake window
- `test_utils.cpp` - CRC-16/KERMIT correctness, determinism and bit-flip detection
- `test_meter_history.cpp` - monthly usage maths, meter-reset handling, the history JSON payload
- `test_runner.cpp` - the single Unity entry point; every test case is registered here

### `test_native_meter_reader/`

Behaviour of the two stateful services, driven against fake hardware.

- `test_meter_reader.cpp` - retry sequence and backoff, post-failure cooldown,
  schedule gating and edge detection, failure classification, publish ordering,
  the manual stop path and the automatic scan after a failure streak
- `test_frequency_manager.cpp` - deep scan lock and abort paths, the calibration
  quality guard, persistence round-trips and adaptive FREQEST tracking

### `test_native_esphome_publisher/`

ESPHomeDataPublisher, built the way the shipped external component builds it.

Runs under `[env:native_esphome]` rather than `[env:native]`, because the whole
publisher is wrapped in `#ifdef USE_ESPHOME` and the ESPHome build needs a
flattened include path. The recording sensor stubs live in
`test/native_esphome/esphome/`, on that environment's include path only, so
`__has_include("esphome/core/log.h")` stays false for the MQTT-mode suites.

Covers sensor population and formatting, the history payload and its
"unavailable" fallbacks, null-argument handling, unit conversions, and the
"first non-null registration wins" rule for the sensors that describe the one
shared radio.

## Fakes and the virtual clock

`test/native_fakes.{h,cpp}` provides the test doubles. It sits in the test root
rather than in a suite folder because PlatformIO compiles everything listed in
`build_src_filter` into *every* host test binary, so all suites have to be able
to resolve the same symbols.

It replaces three things at link time:

| Replaced | Why |
| --- | --- |
| CC1101 free functions (`cc1101_init`, `get_meter_data_for_meter`, ...) | No radio on the host |
| `StorageAbstraction` | No EEPROM or NVS on the host |
| `WifiSerialStream` / `WiFiSerial` | No network stack; log output goes to stdout |

The fake radio answers in one of two modes. Scripted mode consumes a queue of
`tmeter_data` outcomes, repeating the last entry once the script runs out.
Frequency-selective mode places a simulated carrier at a chosen offset and only
answers while the radio is tuned inside the response window, reporting a FREQEST
proportional to the tuning error. That is what makes the deep frequency scan
testable in milliseconds instead of the minutes a real sweep takes.

The shim clock is virtual and starts at a fixed value. Tests move time with
`nativeClockAdvance(ms)`, and `delay()` advances it too, so retry delays and
cooldowns are exercised instantly and deterministically. Nothing ever sleeps.

## Running

```powershell
# One-off: install the tooling (platformio, pytest, esphome, gcovr)
pip install -r scripts/requirements-dev.txt

# Everything CI runs, in the same order
./scripts/run-tests.ps1            # host suites only (a few seconds)
./scripts/run-tests.ps1 -Build     # also compile both firmware targets
./scripts/run-tests.ps1 -All       # also collect coverage
./scripts/run-tests.ps1 -Serial    # show the firmware log output
```

Or drive PlatformIO directly:

```bash
# All MQTT-mode host suites
pio test -e native

# The ESPHome-mode suite
pio test -e native_esphome

# One suite
pio test -e native -f test_embedded_unit

# Verbose
pio test -e native -v
```

`pio test -e huzzah` (or any board environment) deliberately runs nothing:
`test_ignore` in `platformio.ini` excludes every suite, because none of them
needs hardware and all are already covered on the host in CI.

## Native Arduino shim

`test/native_shims/Arduino.h` is a small stand-in for the Arduino core, added to
the include path only by `[env:native]`. It supplies `millis()`, `delay()`,
`constrain()`, `map()`, the `Print`/`Stream` base classes and a `Serial` object,
so that shared service code such as `src/services/schedule_manager.cpp` compiles
on a desktop host.

Serial output from the firmware under test is discarded by default. Set
`EVERBLU_NATIVE_SERIAL=1` to mirror it to stdout when debugging a failure.

Keep the shim minimal. Extend it when a newly host-tested source needs a symbol,
rather than trying to emulate the whole Arduino API.

## Adding a test

1. Add the test function to the relevant `test_*.cpp` file, or create a new one
   in the same suite directory.
2. Declare it and register it with `RUN_TEST(...)` in that suite's runner
   (`test_runner.cpp` for `test_embedded_unit`, `test_native_meter_reader` and
   `test_native_esphome_publisher`, `main()` for `test_native_meter_fixtures`).
3. Do not define `setUp`, `tearDown` or `main` in the individual test files:
   Unity allows only one of each per suite binary.
4. If the code under test is not already compiled for the host, add its `.cpp`
   to `build_src_filter` under `[env:native]` in `platformio.ini`.

## Assertions

Commonly used Unity assertions:

- `TEST_ASSERT_TRUE(condition)`
- `TEST_ASSERT_FALSE(condition)`
- `TEST_ASSERT_EQUAL_INT(expected, actual)`
- `TEST_ASSERT_EQUAL_UINT32(expected, actual)`
- `TEST_ASSERT_EQUAL_HEX16(expected, actual)`
- `TEST_ASSERT_NOT_EQUAL(expected, actual)`
- `TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual)`
- `TEST_ASSERT_NULL(pointer)` / `TEST_ASSERT_NOT_NULL(pointer)`

See the [Unity assertion reference](https://github.com/ThrowTheSwitch/Unity/blob/master/docs/UnityAssertionsReference.md)
for the full list.

## Coverage

CI collects coverage over the host-tested sources with gcovr. Locally:

```bash
pio test -e native
pio test -e native_esphome
gcovr --root . --object-directory .pio/build/ --print-summary \
  --filter src/core/crc_kermit.cpp \
  --filter src/core/radian_parser.cpp \
  --filter src/core/radian_decoder.cpp \
  --filter src/core/utils.cpp \
  --filter src/adapters/implementations/esphome_data_publisher.cpp \
  --filter src/services/frequency_manager.cpp \
  --filter src/services/meter_history.cpp \
  --filter src/services/meter_reader.cpp \
  --filter src/services/schedule_manager.cpp
```
