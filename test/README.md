# Unit Tests

This directory contains unit tests for the EverBlu Meters ESP8266/ESP32 project using the Unity test framework.

## Test Files

- **test_config_validation.cpp** - Tests for configuration validation functions
  - Valid/invalid reading schedules
  - Meter year, serial, frequency validation

- **test_utils.cpp** - Tests for utility functions
  - CRC calculation correctness and determinism
  - Hex display functions

- **test_state_machine.cpp** - Tests for state machine implementation
  - State enumeration validation
  - State transition logic
  - State uniqueness

## Running Tests

### With PlatformIO

```bash
# Run all tests
pio test

# Run specific test environment
pio test -e huzzah

# Run with verbose output
pio test -v

# Run native fixture replay tests (no hardware required)
pio test -e native -v
```

### Native Fixture Replay (GitHub CI compatible)

The `test_native_meter_fixtures` suite replays captured decoded meter frames from:

- `test/fixtures/meter_frames/fixtures.lst`

This suite runs on host (PlatformIO `native` environment), so it is suitable for GitHub Actions.

To generate fixture entries from firmware logs, use:

```bash
python scripts/extract-meter-fixture.py --input meter-capture.log --append --name-prefix capture
```

### Native Full-HAL Meter Read Simulation (GitHub CI compatible)

The `test_native_hal_meter_read` suite runs the **real** standalone (MQTT) radio
and RADIAN protocol firmware (`src/core/cc1101.cpp`, `utils.cpp`, ...) on the host
and drives a complete `get_meter_data_for_meter()` request/response cycle against a
**simulated CC1101 chip** mocked at the SPI/GPIO seam.

Unlike the fixture replay (which starts from an already-decoded frame), this test
exercises the full pipeline: interrogation-frame build, the wake-up/interrogation
TX FIFO feed loop, two-stage `receive_radian_frame()` reception, the 4x-oversampled
`decode_4bitpbit_serial()` DSP, CRC validation and `parse_meter_report()`.

- Shim Arduino/SPI headers: `test/native_hal_shim/`
- Simulated chip + Arduino/SPI HAL: `test/test_native_hal_meter_read/fake_cc1101.*`
- On-air stream encoder (inverse of the decoder): `test/test_native_hal_meter_read/radian_encode.h`

```bash
pio test -e native_hal        # run the end-to-end simulation
pio test -e native_hal -v     # with the simulated read sequence log
```

It runs on the PlatformIO `native` environment (needs a host `g++`), so it is
suitable for GitHub Actions.

### Test Framework

These tests use the [Unity](http://www.throwtheswitch.org/unity) test framework, which is automatically managed by PlatformIO.

## Adding New Tests

1. Create a new test file in this directory: `test_<feature>.cpp`
2. Include the Unity header: `#include <unity.h>`
3. Implement test functions with `test_` prefix
4. Add `setUp()` and `tearDown()` functions if needed
5. Call tests in `setup()` using `RUN_TEST(test_function_name)`

### Test Template

```cpp
#include <unity.h>
#include <Arduino.h>

void setUp(void) {
    // Runs before each test
}

void tearDown(void) {
    // Runs after each test
}

void test_example(void) {
    TEST_ASSERT_EQUAL(expected, actual);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_example);
    UNITY_END();
}

void loop() {
    // Nothing
}
```

## Test Assertions

Common Unity assertions:

- `TEST_ASSERT_TRUE(condition)`
- `TEST_ASSERT_FALSE(condition)`
- `TEST_ASSERT_EQUAL(expected, actual)`
- `TEST_ASSERT_NOT_EQUAL(expected, actual)`
- `TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual)`
- `TEST_ASSERT_NULL(pointer)`
- `TEST_ASSERT_NOT_NULL(pointer)`

See [Unity documentation](https://github.com/ThrowTheSwitch/Unity/blob/master/docs/UnityAssertionsReference.md) for complete list.

## Notes

- Tests run on the actual hardware (ESP8266/ESP32)
- Serial output shows test results
- Failed tests will stop execution and report the failure
- Use mocks/stubs for hardware-dependent code when needed
