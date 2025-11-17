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
```

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
