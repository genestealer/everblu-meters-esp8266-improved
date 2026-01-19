# ESPHome Integration for EverBlu Cyble Enhanced Meters

Complete ESPHome custom component for reading EverBlu Cyble Enhanced water and gas meters. This integration provides seamless Home Assistant integration without requiring an MQTT broker.

## Documentation

### For End Users

- **[ESPHome Integration Guide](docs/ESPHOME_INTEGRATION_GUIDE.md)** - Complete installation and configuration guide
  - Hardware requirements and wiring
  - Installation steps
  - Full configuration reference
  - Sensor documentation
  - Troubleshooting guide

- **[Home Assistant Integration](docs/ESPHOME_HOME_ASSISTANT_INTEGRATION.md)** - Accessing meter data and historical readings in Home Assistant
  - Template sensors and utility meters
  - Historical data extraction
  - Long-term consumption tracking
  - Calibration and hardware change handling

- **[Configuration Reference](#configuration-reference)** - Quick lookup for common tasks on this page
  - Configuration parameter tables
  - Common configuration patterns
  - Wiring diagrams
  - Troubleshooting quick fixes

### For Developers

- **[Developer Guide](docs/DEVELOPER_GUIDE.md)** - Technical architecture and integration patterns
  - Architecture overview and design principles
  - Dependency injection pattern
  - ESPHome integration patterns
  - How to extend the component
  - Advanced customization

- **[Component README](components/everblu_meter/README.md)** - Component structure and development
  - Component file structure
  - Architecture diagram
  - Build process
  - Development guidelines

- **[Build Notes](docs/ESPHOME_BUILD_NOTES.md)** - Advanced build configuration
  - Source file access strategies
  - Distribution preparation
  - Build troubleshooting
  - **Important**: When to run `prepare-component-release` scripts

> Developer Note: If you modify source files in `src/`, you must run `prepare-component-release.ps1/.sh` to update the `ESPHOME-release` folder.

## Quick Start

### 1. Use as External Component

The component is ready to use directly from the `ESPHOME-release` folder. Add to your ESPHome YAML configuration:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/genestealer/everblu-meters-esp8266-improved
      ref: main
      path: ESPHOME-release
    components: [ everblu_meter ]
    refresh: 1d

# Or use locally:
# external_components:
#   - source:
#       type: local
#       path: path/to/everblu-meters-esp8266-improved/ESPHOME-release
#     components: [ everblu_meter ]

time:
  - platform: homeassistant
    id: ha_time

everblu_meter:
  meter_year: 21
  meter_serial: 12345678
  meter_type: water
  gdo0_pin: 4
  time_id: ha_time
  
  volume:
    name: "Water Volume"
    device_class: water
    state_class: total_increasing
  
  status:
    name: "Status"
```

For a manual local install, copy the component into your ESPHome `custom_components` folder:

```bash
cp -r ESPHOME-release/everblu_meter /config/esphome/custom_components/
```

### 2. Build and Upload

```bash
esphome run your-config.yaml
```

## Configuration Reference

### Key Parameters

| Parameter | Type | Default | Required | Description |
|-----------|------|---------|----------|-------------|
| `meter_year` | int | - | Yes | Meter year (0-99) |
| `meter_serial` | int | - | Yes | Serial number |
| `meter_type` | enum | - | Yes | `water` or `gas` |
| `gdo0_pin` | int | - | Yes | CC1101 GDO0 GPIO |
| `time_id` | id | - | Yes | Time component ID |
| `frequency` | float | 433.82 | No | RF frequency (MHz) |
| `auto_scan` | bool | true | No | Auto frequency scan |
| `schedule` | enum | Monday-Friday | No | Reading schedule |
| `read_hour` | int | 10 | No | Read hour (0-23) |
| `read_minute` | int | 0 | No | Read minute (0-59) |
| `max_retries` | int | 10 | No | Max read attempts |
| `retry_cooldown` | duration | 1h | No | Cooldown time |
| `gas_volume_divisor` | int | 100 | No | Gas divisor (100/1000) |

### Schedule Options

- Monday-Friday - Weekdays (default)
- Saturday - Saturdays only
- Sunday - Sundays only
- Everyday - All days

### Custom Schedule Example

```yaml
everblu_meter:
  meter_year: 21
  meter_serial: 12345678
  gdo0_pin: 4
  meter_type: water
  time_id: ha_time
  
  # Weekend-only, early morning
  schedule: Saturday
  read_hour: 6
  read_minute: 30
  
  # Aggressive retries
  max_retries: 15
  retry_cooldown: 30min
  
  volume:
    name: "Volume"
```

### Logging

```yaml
logger:
  level: DEBUG
  logs:
    everblu_meter: VERBOSE  # Maximum detail
    sensor: WARN            # Reduce sensor noise
```

### Timezone Adjustment

```yaml
everblu_meter:
  timezone_offset: -5  # Hours from UTC
```

## Features

- **Native ESPHome Integration**: Works seamlessly with Home Assistant via ESPHome API
- **Automatic Discovery**: Sensors appear automatically in Home Assistant
- **Scheduled Readings**: Configure when and how often to read the meter
- **Frequency Management**: Automatic frequency scanning and optimization
- **Comprehensive Monitoring**: Track signal quality, battery life, and reading statistics
- **Multiple Meter Types**: Supports both water and gas meters
- **Retry Logic**: Configurable retry attempts with cooldown periods
- **Low Power**: Efficient reading schedule minimizes power consumption

## Example Configurations

Three complete example configurations are provided:

1. **[Water Meter Example](example-water-meter.yaml)** - Full-featured water meter configuration
2. **[Gas Meter Example](example-gas-meter-minimal.yaml)** - Minimal gas meter configuration
3. **[Advanced Example](example-advanced.yaml)** - Advanced features and customization

## Hardware Requirements

- **ESP8266** (e.g., D1 Mini) or **ESP32** board
- **CC1101** RF transceiver module (868/915 MHz version)
- **EverBlu Cyble Enhanced** meter with RF module installed

### Wiring (ESP8266 D1 Mini)

| CC1101 Pin | D1 Mini | GPIO |
|------------|---------|------|
| VCC        | 3.3V    | -    |
| GND        | GND     | -    |
| SCK        | D5      | 14   |
| MISO       | D6      | 12   |
| MOSI       | D7      | 13   |
| CSN        | D8      | 15   |
| GDO0       | D1      | 5    |
| GDO2       | D2      | 4    |

### Wiring (ESP32)

| CC1101 | ESP32 |
|--------|-------|
| VCC    | 3.3V  |
| GND    | GND   |
| SCK    | 18    |
| MISO   | 19    |
| MOSI   | 23    |
| CSN    | 5     |
| GDO0   | 4     |
| GDO2   | 2     |

Important: The CC1101 requires 3.3V power. Do not connect to 5V!

## Benefits

### vs. Standalone MQTT Mode

| Feature | ESPHome Mode | MQTT Mode |
|---------|--------------|-----------|
| Configuration | YAML | C++ defines |
| Integration | ESPHome API | MQTT Broker |
| Discovery | Automatic | Manual |
| Updates | OTA | OTA |
| Logging | ESPHome logs | Serial/WiFi |
| Complexity | Low | Medium |

### ESPHome Mode Advantages

- **Simple Configuration**: YAML-based instead of C++ defines
- **Native Integration**: Direct Home Assistant integration
- **No MQTT Broker**: Reduces infrastructure complexity
- **All ESPHome Features**: Web server, logging, diagnostics, etc.
- **Automatic Discovery**: Sensors appear in Home Assistant automatically

## Home Assistant Best Practice: Utility Meter Helper

**Recommended:** Create a Home Assistant Utility Meter helper to preserve historical data across platform or meter changes.

**Why use a utility meter helper?**

If you switch between ESPHome and MQTT, change meter serial numbers, or replace hardware, a utility meter helper acts as a stable interface. You simply update the source sensor in the helper configuration, and all your historical data, dashboards, and automations remain intact.

**Quick Setup:**

1. In Home Assistant: **Settings** → **Devices & Services** → **Helpers**
2. **Create Helper** → **Utility Meter**
3. Configure:
   - **Name**: "Master Water Meter" (or "Master Gas Meter")
   - **Input sensor**: Your volume sensor (e.g., `sensor.water_volume`)
   - **Meter type**: Daily/monthly/yearly or none

**Benefits:**
- Preserve history when switching ESPHome ↔ MQTT
- Seamless meter serial number updates
- Single point to update for hardware changes
- Stable entity ID for dashboards and automations

**Example:**
```yaml
utility_meter:
  master_water_meter:
    source: sensor.water_volume  # Just update this when you change platforms
    name: Master Water Meter
```

When changing platforms or meters, update only the `source` - your history stays intact!

### Historical Data from Meter

The ESPHome component exposes a **history text sensor** containing 12 months of historical readings stored directly in the meter. This data is retrieved from the meter itself and provided in JSON format.

**Example JSON payload:**
```json
{
  "history": [605696, 614107, 621401, 630219, 640054, 652789, 667441, 684214, 700917, 712720, 721549, 728836],
  "monthly_usage": [605696, 8411, 7294, 8818, 9835, 12735, 14652, 16773, 16703, 11803, 8829, 7287],
  "current_month_usage": 13043,
  "months_available": 12
}
```

**Data Structure:**
- `history`: 12 monthly readings (oldest to newest) in L or m³
- `monthly_usage`: 12 monthly consumption values (first is oldest reading, rest are differences)
- `current_month_usage`: Current month consumption
- `months_available`: Months of data (typically 12)

**Use Cases:**
- Bootstrap Home Assistant with 12 months of existing data on first setup
- Analyze historical consumption patterns
- Compare current vs. previous month usage
- Verify readings against utility bills
- Pre-populate energy dashboards

**Accessing in Home Assistant:**

The history sensor appears as `sensor.{device_name}_meter_history_json` (e.g., `sensor.water_meter_monitor_meter_history_json`). Parse it using template sensors:

```yaml
template:
  - sensor:
      - name: "Last Month Usage"
        unit_of_measurement: "L"
        state: >-
          {% set data = states('sensor.water_meter_monitor_meter_history_json') | from_json %}
          {{ data.monthly_usage[-1] if data.monthly_usage else 0 }}
      
      - name: "Average Monthly Usage"
        unit_of_measurement: "L"
        state: >-
          {% set data = states('sensor.water_meter_monitor_meter_history_json') | from_json %}
          {% if data.monthly_usage %}
            {{ (data.monthly_usage[1:] | sum / (data.monthly_usage[1:] | length)) | round(0) }}
          {% else %}
            0
          {% endif %}
```

**Note:** This is historical data from the meter's internal memory, updated when the meter is read. It's separate from Home Assistant's own historical database.

## Architecture

The component uses a clean adapter pattern to separate platform-specific code from core meter reading logic:

```
EverbluMeterComponent (ESPHome)
├── ESPHomeConfigProvider    → Configuration from YAML
├── ESPHomeTimeProvider      → Time synchronization
├── ESPHomeDataPublisher     → Sensor publishing
└── MeterReader (shared)
    ├── CC1101               → Radio hardware
    ├── FrequencyManager     → Frequency optimization
    └── ScheduleManager      → Reading schedule
```

**Key Benefits**:
- ~95% code shared between ESPHome and standalone modes
- Clean separation of concerns
- Easy to test and maintain
- Extensible for other platforms

## Available Sensors

### Numeric Sensors
- **volume** - Current meter reading (L or m³)
- **battery** - Estimated battery life (years)
- **counter** - Alternative volume counter
- **rssi** / **rssi_percentage** - Radio signal strength
- **lqi** / **lqi_percentage** - Link quality indicator
- **time_start** / **time_end** - Reading timing
- **total_attempts** / **successful_reads** / **failed_reads** - Statistics

### Text Sensors
- **status** - Current meter status (Idle/Reading/Success/Error)
- **error** - Last error message
- **radio_state** - Radio state (Init/Scanning/Receiving/Idle)
- **timestamp** - Last successful reading time

### Binary Sensors
- **active_reading** - Whether a reading is currently in progress

## Common Configuration Patterns

### Water Meter - Basic
```yaml
everblu_meter:
  meter_year: 21
  meter_serial: 12345678
  meter_type: water
  gdo0_pin: 4
  time_id: ha_time
  volume:
    name: "Water Volume"
```

### Gas Meter - Basic
```yaml
everblu_meter:
  meter_year: 22
  meter_serial: 87654321
  meter_type: gas
  gdo0_pin: 4
  time_id: ha_time
  gas_volume_divisor: 100
  volume:
    name: "Gas Volume"
```

### With Full Monitoring
```yaml
everblu_meter:
  meter_year: 21
  meter_serial: 12345678
  meter_type: water
  gdo0_pin: 4
  time_id: ha_time
  
  volume:
    name: "Volume"
  battery:
    name: "Battery"
  rssi_percentage:
    name: "Signal"
  status:
    name: "Status"
  active_reading:
    name: "Reading Active"
```

## Troubleshooting

### Quick Fixes

**No readings received:**
```yaml
everblu_meter:
  auto_scan: true        # Enable frequency scanning
  max_retries: 15        # Increase retry attempts
```

**Poor signal quality:**
```yaml
everblu_meter:
  frequency: 433.85      # Try different frequency
  auto_scan: false       # Disable auto-scan once optimal found
```

**Wrong volume reading (gas meters):**
```yaml
everblu_meter:
  gas_volume_divisor: 1000  # Try 100 or 1000
```

**Incorrect time or timezone:**
```yaml
everblu_meter:
  timezone_offset: -5  # Hours from UTC
```

For detailed troubleshooting, see the [Integration Guide](docs/ESPHOME_INTEGRATION_GUIDE.md#troubleshooting).

## \ud83d\dcd3 License

MIT License - See [LICENSE.md](../LICENSE.md)

## \ud83d\de4f Credits

Based on the EverBlu Meters ESP8266 project with architectural improvements for reusability and ESPHome integration.

## \ud83d\udd17 Links

- **Main Project**: [Main README](../README.md)
- **GitHub Repository**: https://github.com/yourusername/everblu-meters-esp8266-improved
- **ESPHome Documentation**: https://esphome.io/
- **Home Assistant**: https://www.home-assistant.io/

---

**Need Help?**
- \ud83d\udcd6 Start with the [Integration Guide](docs/ESPHOME_INTEGRATION_GUIDE.md)
- \ud83c\udfe0 See [Home Assistant Integration](docs/ESPHOME_HOME_ASSISTANT_INTEGRATION.md) for accessing meter data in Home Assistant
- \ud83d\dd0d See the Configuration Reference above for parameters and quick fixes
- \ud83d\udc1b See [Troubleshooting](docs/ESPHOME_INTEGRATION_GUIDE.md#troubleshooting) for common issues
- \ud83d\dc68\u200d\ud83d\udcbb Developers: See [Developer Guide](docs/DEVELOPER_GUIDE.md) for architecture details
