# EverBlu Meter ESPHome Component

This directory contains the ESPHome custom component for reading EverBlu Cyble Enhanced water and gas meters.

## Quick Start

1. **Copy the component** to your ESPHome configuration directory:
   ```bash
  cp -r everblu_meter /config/esphome/custom_components/
   ```

2. **Create your configuration** (see examples in parent directory):
   ```yaml
  external_components:
    - source:
        type: local
        path: custom_components
      components: [ everblu_meter ]
   
   everblu_meter:
     meter_year: 21
     meter_serial: 12345678
     gdo0_pin: 4
     meter_type: water
     volume:
       name: "Water Volume"
   ```

3. **Compile and upload**:
   ```bash
   esphome run your-config.yaml
   ```

## Component Structure

```
everblu_meter/
├── __init__.py              # Python config schema
├── everblu_meter.h          # Component header
└── everblu_meter.cpp        # Component implementation
```

### `__init__.py`

Defines the ESPHome configuration schema, validates user input, and generates C++ code during compilation. Handles:

- Configuration validation (meter year, serial, frequency ranges)
- Sensor registration (volume, battery, RSSI, status, etc.)
- Code generation for component setup

### `everblu_meter.h` / `everblu_meter.cpp`

Main C++ component that integrates with ESPHome. Implements:

- **EverbluMeterComponent**: ESPHome PollingComponent
- **Lifecycle methods**: `setup()`, `loop()`, `update()`, `dump_config()`
- **Adapter instantiation**: Creates ESPHome-specific adapters
- **Sensor linking**: Connects meter data to ESPHome sensors

## Architecture

The component uses the adapter pattern to integrate the meter reading logic with ESPHome:

```
EverbluMeterComponent
├── ESPHomeConfigProvider    (reads from YAML)
├── ESPHomeTimeProvider      (wraps ESPHome time component)
├── ESPHomeDataPublisher     (publishes to ESPHome sensors)
└── MeterReader              (core meter reading logic)
    └── CC1101               (radio hardware driver)
```

### Adapters

Located in `../../src/adapters/implementations/`:

- **ESPHomeConfigProvider**: Provides configuration from YAML instead of compile-time defines
- **ESPHomeTimeProvider**: Wraps ESPHome's time component for time synchronization
- **ESPHomeDataPublisher**: Publishes meter data to ESPHome sensors instead of MQTT

### Core Logic

Located in `../../src/`:

- **services/meter_reader**: Platform-agnostic meter reading orchestration
- **core/cc1101**: CC1101 radio driver
- **services/frequency_manager**: Frequency scanning and optimization
- **services/schedule_manager**: Reading schedule management

## Configuration Options

See [ESPHOME_INTEGRATION_GUIDE.md](../ESPHOME_INTEGRATION_GUIDE.md) for complete documentation.

### Required

- `meter_year`: Meter manufacture year (last 2 digits)
- `meter_serial`: Meter serial number
- `gdo0_pin`: GPIO number connected to CC1101 GDO0 (sync detect)
- `meter_type`: `water` or `gas`

### Optional

- `frequency`: RF frequency in MHz (default: 433.82)
- `auto_scan`: Auto-scan for optimal frequency (default: true)
- `schedule`: Reading schedule (default: Monday-Friday)
- `read_hour`/`read_minute`: Time to read (default: 10:00)
- `max_retries`: Read attempts (default: 10)
- `retry_cooldown`: Cooldown duration (default: 1h)
- `adaptive_threshold`: How many successful reads before adjusting frequency (default: 1)
  - `1`: Adjust after every read (best for infrequent daily readings or drifting frequencies)
  - `5`: Adjust after 5 successful reads (good balance)
  - `10+`: Adjust only after many reads (best for stable, frequent readings)
- `request_reading_button`: Optional ESPHome button to trigger an on-demand reading
- `frequency_scan_button`: Optional ESPHome button to start a narrow frequency scan

### Sensors

All sensors are optional. Available types:

- **Numeric**: volume, battery, counter, RSSI, LQI, timing, statistics
- **Text**: status, error, radio_state, timestamp
- **Binary**: active_reading

## Examples

### Minimal Configuration

```yaml
everblu_meter:
  meter_year: 21
  meter_serial: 12345678
  meter_type: water
  
  volume:
    name: "Water Volume"
```

### With Monitoring

```yaml
everblu_meter:
  meter_year: 21
  meter_serial: 12345678
  meter_type: water
  
  volume:
    name: "Water Volume"
  battery:
    name: "Battery Life"
  rssi_percentage:
    name: "Signal Quality"
  status:
    name: "Status"
```

### Complete Examples

See parent directory for complete examples:
- `../example-water-meter.yaml` - Full featured water meter
- `../example-gas-meter-minimal.yaml` - Minimal gas meter
- `../example-advanced.yaml` - Advanced features

## Development

### Building

The component is compiled as part of ESPHome's build process. It automatically includes the necessary source files from `../../src/`.

### Dependencies

**ESPHome Components:**
- `sensor` - Numeric sensors
- `text_sensor` - Text sensors
- `binary_sensor` - Binary sensors
- `time` - Time synchronization

**C++ Requirements:**
- C++11 or later
- SPI library (for CC1101)

### Conditional Compilation

The adapters use `#ifdef USE_ESPHOME` to enable ESPHome-specific code. This allows the same codebase to support both standalone (MQTT) and ESPHome modes.

## Troubleshooting

### Component Not Found

Ensure the `external_components` path is correct:

```yaml
external_components:
  - source:
      type: local
      path: custom_components  # Relative to your YAML file
    components: [ everblu_meter ]
```

### Compilation Errors

1. **Missing headers**: Verify all files in `../../src/` are present
2. **SPI errors**: Ensure SPI is configured for your board
3. **GDO0 not defined**: Ensure `gdo0_pin` is set in your YAML
4. **Adapter errors**: Check `USE_ESPHOME` is defined during compilation

### Runtime Errors

1. **No readings**: Check wiring, frequency, and schedule configuration
2. **Time errors**: Ensure time component is configured and synchronized
3. **Sensor errors**: Verify sensors are registered in YAML

See [ESPHOME_INTEGRATION_GUIDE.md](../ESPHOME_INTEGRATION_GUIDE.md) for detailed troubleshooting.

## License

MIT License - See [LICENSE.md](../../LICENSE.md)

## Credits

Based on the EverBlu Meters ESP8266 project with architectural improvements for reusability.

## Links

- **Documentation**: [ESPHOME_INTEGRATION_GUIDE.md](../ESPHOME_INTEGRATION_GUIDE.md)
- **Main README**: [../../README.md](../../README.md)
- **GitHub**: https://github.com/yourusername/everblu-meters-esp8266-improved
