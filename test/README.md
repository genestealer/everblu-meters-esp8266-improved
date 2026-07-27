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

To add a fixture from a firmware log:

```bash
python scripts/extract-meter-fixture.py --input meter-capture.log --append --name-prefix capture
```

### `test_embedded_unit/`

Pure logic tests for the platform-neutral parts of the firmware.

- `test_config_validation.cpp` - meter code parsing, schedule string validation
- `test_schedule_manager.cpp` - reading days, UTC/local conversion, clamping, auto-alignment to the meter wake window
- `test_utils.cpp` - CRC-16/KERMIT correctness, determinism and bit-flip detection
- `test_runner.cpp` - the single Unity entry point; every test case is registered here

## Running

```bash
# All host suites
pio test -e native

# One suite
pio test -e native -f test_embedded_unit

# Verbose
pio test -e native -v
```

`pio test -e huzzah` (or any board environment) deliberately runs nothing:
`test_ignore` in `platformio.ini` excludes both suites, because neither needs
hardware and both are already covered on the host in CI.

## Native Arduino shim

`test/native_shims/Arduino.h` is a small stand-in for the Arduino core, added to
the include path only by `[env:native]`. It supplies `millis()`, `delay()`,
`constrain()`, `map()` and a `Serial` object so that shared service code such as
`src/services/schedule_manager.cpp` compiles on a desktop host.

Serial output from the firmware under test is discarded by default. Set
`EVERBLU_NATIVE_SERIAL=1` to mirror it to stdout when debugging a failure.

Keep the shim minimal. Extend it when a newly host-tested source needs a symbol,
rather than trying to emulate the whole Arduino API.

## Adding a test

1. Add the test function to the relevant `test_*.cpp` file, or create a new one
   in the same suite directory.
2. Declare it and register it with `RUN_TEST(...)` in that suite's runner
   (`test_runner.cpp` for `test_embedded_unit`, `main()` for
   `test_native_meter_fixtures`).
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
gcovr --root . --object-directory .pio/build/native/ --print-summary \
  --filter src/core/crc_kermit.cpp \
  --filter src/core/radian_parser.cpp \
  --filter src/core/radian_decoder.cpp \
  --filter src/services/schedule_manager.cpp
```
