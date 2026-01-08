# ESPHome Integration Testing Checklist

This checklist helps verify that the ESPHome integration works correctly across different scenarios.

## Pre-Testing Setup

- [ ] ESPHome installed (version 2023.x or later)
- [ ] Home Assistant running (optional but recommended)
- [ ] CC1101 module available
- [ ] ESP8266 or ESP32 board available
- [ ] Meter information known (year, serial number, type)

## File Structure Verification

- [ ] `ESPHOME/components/everblu_meter/__init__.py` exists
- [ ] `ESPHOME/components/everblu_meter/everblu_meter.h` exists
- [ ] `ESPHOME/components/everblu_meter/everblu_meter.cpp` exists
- [ ] `src/adapters/implementations/esphome_config_provider.h` exists
- [ ] `src/adapters/implementations/esphome_config_provider.cpp` exists
- [ ] `src/adapters/implementations/esphome_time_provider.h` exists
- [ ] `src/adapters/implementations/esphome_time_provider.cpp` exists
- [ ] `src/adapters/implementations/esphome_data_publisher.h` exists
- [ ] `src/adapters/implementations/esphome_data_publisher.cpp` exists

## Configuration Validation

### Minimal Configuration
- [ ] Copy `example-gas-meter-minimal.yaml` to test config
- [ ] Update meter_year, meter_serial
- [ ] Run `esphome config test-config.yaml`
- [ ] No configuration errors
- [ ] Component loads successfully

### Full Configuration
- [ ] Copy `example-water-meter.yaml` to test config
- [ ] Update all required fields
- [ ] Run `esphome config test-config.yaml`
- [ ] All sensors validate correctly
- [ ] No warnings about unknown options

### Invalid Configuration Tests
- [ ] Test meter_year out of range (100) - should fail
- [ ] Test meter_year negative (-1) - should fail
- [ ] Test frequency too low (299 MHz) - should fail
- [ ] Test frequency too high (929 MHz) - should fail
- [ ] Test invalid schedule value - should fail
- [ ] Test missing required field (meter_serial) - should fail

## Compilation Tests

### ESP8266 Compilation
- [ ] Copy component to ESPHome directory
- [ ] Create test config for ESP8266 (D1 Mini)
- [ ] Run `esphome compile test-esp8266.yaml`
- [ ] Compilation succeeds
- [ ] No warnings about missing headers
- [ ] Binary size reasonable (<1MB)

### ESP32 Compilation
- [ ] Create test config for ESP32
- [ ] Run `esphome compile test-esp32.yaml`
- [ ] Compilation succeeds
- [ ] No warnings
- [ ] Binary size reasonable

### Multiple Sensor Configurations
- [ ] Config with only volume sensor - compiles
- [ ] Config with all numeric sensors - compiles
- [ ] Config with all text sensors - compiles
- [ ] Config with all binary sensors - compiles
- [ ] Config with all sensors - compiles
- [ ] Config with no sensors - compiles (should warn?)

## Upload and Boot Tests

### First Boot
- [ ] Upload firmware via USB
- [ ] Device boots successfully
- [ ] WiFi connects
- [ ] Time synchronizes
- [ ] CC1101 initializes
- [ ] No crashes in logs
- [ ] Status LED/indicator works (if configured)

### Logging Output
- [ ] Component logs "Setting up EverBlu Meter..."
- [ ] Adapter creation logged
- [ ] MeterReader initialization logged
- [ ] CC1101 configuration logged
- [ ] Frequency calibration logged (if auto_scan enabled)
- [ ] Configuration dump shows all settings

### Error Handling
- [ ] Boot without time component configured - logs warning
- [ ] Boot with invalid CC1101 wiring - logs error
- [ ] Boot without sensors - works without errors

## Sensor Functionality

### Sensor Discovery
- [ ] Volume sensor appears in ESPHome
- [ ] Battery sensor appears
- [ ] RSSI sensor appears
- [ ] LQI sensor appears
- [ ] Status text sensor appears
- [ ] Active reading binary sensor appears
- [ ] All configured sensors appear

### Sensor Updates
- [ ] Volume sensor shows reasonable value
- [ ] Battery sensor shows years (positive number)
- [ ] RSSI sensor shows dBm (negative number)
- [ ] LQI sensor shows 0-255
- [ ] RSSI percentage shows 0-100%
- [ ] LQI percentage shows 0-100%
- [ ] Status sensor shows "Idle" initially

### Sensor Properties
- [ ] Volume sensor has correct unit (L or m³)
- [ ] Volume sensor has device_class (water or gas)
- [ ] Volume sensor has state_class (total_increasing)
- [ ] Battery sensor has correct unit (years)
- [ ] RSSI sensor has correct unit (dBm)
- [ ] RSSI sensor has device_class (signal_strength)
- [ ] Text sensors show strings
- [ ] Binary sensors show true/false

## Home Assistant Integration

### API Connection
- [ ] Device connects to Home Assistant API
- [ ] Encryption key works (if configured)
- [ ] Connection stays stable

### Entity Discovery
- [ ] All sensors appear in HA as entities
- [ ] Entity names match configuration
- [ ] Entity IDs are reasonable
- [ ] Icons display correctly
- [ ] Device class applied correctly

### Entity Updates
- [ ] Sensor values update in HA
- [ ] Updates appear without long delays
- [ ] Historical data records correctly
- [ ] Statistics work for total_increasing sensors

### Device Information
- [ ] Device appears in ESPHome integration
- [ ] Device has correct name
- [ ] Firmware version shown
- [ ] Device area assignable

## Reading Functionality

### Scheduled Reading
- [ ] Configure schedule for current day
- [ ] Wait for scheduled time
- [ ] Reading triggers automatically
- [ ] Status sensor changes to "Reading"
- [ ] Volume sensor updates
- [ ] Timestamp sensor updates
- [ ] Active reading sensor turns on/off

### Manual Reading Trigger
- [ ] Create automation/button to trigger reading
- [ ] Trigger manual reading
- [ ] Reading executes
- [ ] Sensors update

### Failed Reading Handling
- [ ] Move device away from meter (simulate failure)
- [ ] Attempt reading
- [ ] Status sensor shows "Error"
- [ ] Error sensor shows error message
- [ ] Failed reads counter increments
- [ ] Device doesn't crash

## Frequency Management

### Auto-Scan
- [ ] Enable auto_scan in config
- [ ] First boot performs wide scan
- [ ] Logs show frequencies scanned
- [ ] Optimal frequency detected
- [ ] Reading succeeds with detected frequency

### Manual Frequency
- [ ] Disable auto_scan
- [ ] Set specific frequency
- [ ] Reading uses configured frequency
- [ ] No scanning occurs

### Frequency Adjustment
- [ ] Change frequency in config
- [ ] OTA update
- [ ] New frequency used
- [ ] Reading still works

## Schedule Configuration

### Different Schedule Types
- [ ] Monday-Friday schedule works
- [ ] Saturday schedule works
- [ ] Sunday schedule works
- [ ] Everyday schedule works
- [ ] Reading only happens on configured days

### Time Configuration
- [ ] read_hour=0 (midnight) works
- [ ] read_hour=12 (noon) works
- [ ] read_hour=23 (11 PM) works
- [ ] read_minute=0 works
- [ ] read_minute=30 works
- [ ] read_minute=59 works

### Timezone Handling
- [ ] timezone_offset=0 works
- [ ] timezone_offset=-5 works
- [ ] timezone_offset=+1 works
- [ ] Times align correctly with timezone

## Retry Logic

### Retry Configuration
- [ ] max_retries=1 - only tries once
- [ ] max_retries=10 - tries up to 10 times
- [ ] max_retries=20 - tries up to 20 times

### Cooldown Period
- [ ] retry_cooldown=10min works
- [ ] retry_cooldown=1h works
- [ ] retry_cooldown=24h works
- [ ] Cooldown prevents excessive retries

### Retry Statistics
- [ ] total_attempts sensor increments
- [ ] successful_reads sensor increments on success
- [ ] failed_reads sensor increments on failure
- [ ] Statistics accumulate correctly

## OTA Updates

### Configuration Changes
- [ ] Change sensor name in config
- [ ] OTA update
- [ ] Sensor name changes in HA
- [ ] No issues after update

### Firmware Updates
- [ ] Make code change
- [ ] Compile and OTA update
- [ ] Update succeeds
- [ ] Device boots correctly
- [ ] Sensors still work

### Update Failure Recovery
- [ ] Simulate failed OTA (disconnect during update)
- [ ] Device recovers (safe mode or previous version)
- [ ] Can retry OTA successfully

## Memory and Performance

### Memory Usage
- [ ] Check free heap after boot (should be >20KB)
- [ ] Monitor heap during reading
- [ ] No memory leaks over 24 hours
- [ ] No crashes from out-of-memory

### CPU Usage
- [ ] Device responsive during idle
- [ ] Device responsive during reading
- [ ] Web server responsive (if enabled)
- [ ] Other components work during reading

### Network Performance
- [ ] WiFi connection stable
- [ ] API connection stable
- [ ] Sensor updates timely
- [ ] No packet loss

## Advanced Features

### Multiple Sensors
- [ ] Configure 5+ sensors
- [ ] All update correctly
- [ ] No performance issues
- [ ] No memory issues

### Filter Configuration
- [ ] Apply throttle filter to volume sensor
- [ ] Apply delta filter to volume sensor
- [ ] Filters work as expected
- [ ] Updates still occur

### Automation Triggers
- [ ] Create automation on status change
- [ ] Automation triggers correctly
- [ ] Create automation on volume change
- [ ] Automation works

### Template Sensors
- [ ] Create consumption calculation template
- [ ] Template updates correctly
- [ ] Math is correct
- [ ] No errors in logs

## Gas Meter Specific Tests

### Gas Configuration
- [ ] Set meter_type to gas
- [ ] Set gas_volume_divisor to 100
- [ ] Volume shows in m³
- [ ] Device class is gas
- [ ] Icon is gas meter

### Gas Volume Divisor
- [ ] Try gas_volume_divisor=100
- [ ] Try gas_volume_divisor=1000
- [ ] Reading values make sense
- [ ] Units display correctly

## Documentation Tests

### Examples Work
- [ ] `example-water-meter.yaml` compiles
- [ ] `example-gas-meter-minimal.yaml` compiles
- [ ] `example-advanced.yaml` compiles
- [ ] All examples have valid syntax

### Documentation Accuracy
- [ ] Wiring diagrams match actual pins
- [ ] Configuration examples work
- [ ] Troubleshooting steps are helpful
- [ ] Links in docs work

### Code Comments
- [ ] Header files have documentation
- [ ] Complex functions have comments
- [ ] Python schema has descriptions
- [ ] Examples have explanatory comments

## Edge Cases and Stress Tests

### Boundary Values
- [ ] meter_year=0 (minimum)
- [ ] meter_year=99 (maximum)
- [ ] frequency=433.0 (minimum)
- [ ] frequency=434.8 (maximum)
- [ ] All work without errors

### Long-Term Stability
- [ ] Run for 24 hours
- [ ] No crashes
- [ ] No memory leaks
- [ ] Multiple readings succeed
- [ ] Logs don't fill up

### Network Issues
- [ ] WiFi disconnect during idle
- [ ] Device reconnects automatically
- [ ] WiFi disconnect during reading
- [ ] Reading retries after reconnect
- [ ] API reconnects after WiFi restore

### Time Sync Issues
- [ ] Boot without time sync
- [ ] Time syncs later
- [ ] Readings work after sync
- [ ] No crashes from missing time

## Final Verification

### User Experience
- [ ] Setup is straightforward
- [ ] Configuration is intuitive
- [ ] Errors are clear
- [ ] Documentation is helpful

### Code Quality
- [ ] No compiler warnings
- [ ] Code follows ESPHome conventions
- [ ] Proper error handling
- [ ] Memory management correct

### Integration Quality
- [ ] Sensors follow HA naming conventions
- [ ] Device classes appropriate
- [ ] State classes appropriate
- [ ] Icons meaningful

### Production Readiness
- [ ] All tests pass
- [ ] Documentation complete
- [ ] Examples work
- [ ] Ready for users

## Test Results Summary

**Date**: _______________
**Tester**: _______________
**ESPHome Version**: _______________
**Board Tested**: _______________

**Overall Result**: [ ] PASS [ ] FAIL

**Notes**:
________________________________________________________________
________________________________________________________________
________________________________________________________________

**Issues Found**:
________________________________________________________________
________________________________________________________________
________________________________________________________________

**Recommendations**:
________________________________________________________________
________________________________________________________________
________________________________________________________________
