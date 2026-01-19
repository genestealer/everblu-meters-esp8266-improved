# ESPHome Integration for EverBlu Cyble Enhanced Meters

Complete ESPHome custom component for reading EverBlu Cyble Enhanced water and gas meters. This integration provides seamless Home Assistant integration without requiring an MQTT broker.

## 📚 Documentation

### For End Users

- **[ESPHome Integration Guide](ESPHOME_INTEGRATION_GUIDE.md)** - Complete installation and configuration guide
  - Hardware requirements and wiring
  - Installation steps
  - Full configuration reference
  - Sensor documentation
  - Troubleshooting guide
  
- **[Quick Reference](QUICK_REFERENCE.md)** - Quick lookup for common tasks
  - Configuration parameter tables
  - Common configuration patterns
  - Wiring diagrams
  - Troubleshooting quick fixes

### For Developers

- **[Developer Guide](DEVELOPER_GUIDE.md)** - Technical architecture and integration patterns
  - Architecture overview and design principles
  - Dependency injection pattern
  - ESPHome integration patterns
  - How to extend the component
  - Advanced customization

- **[Component README](../components/everblu_meter/README.md)** - Component structure and development
  - Component file structure
  - Architecture diagram
  - Build process
  - Development guidelines

- **[Build Notes](ESPHOME_BUILD_NOTES.md)** - Advanced build configuration
  - Source file access strategies
  - Distribution preparation
  - Build troubleshooting

## 🚀 Quick Start

### 1. Prepare the Component

Before using the component, prepare it for distribution:

```bash
# From repository root
# Windows PowerShell
pwsh ESPHOME/prepare-component-release.ps1

# Linux/macOS
bash ESPHOME/prepare-component-release.sh
```

### 2. Use as External Component

Add to your ESPHome YAML configuration:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/genestealer/everblu-meters-esp8266-improved/tree/fix-esphome/ESPHOME-release.git
      ref: main
    components: [ everblu_meter ]
    refresh: 1d

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

### 3. Build and Upload

```bash
esphome run your-config.yaml
```

## 📋 Features

- **Native ESPHome Integration**: Works seamlessly with Home Assistant via ESPHome API
- **Automatic Discovery**: Sensors appear automatically in Home Assistant
- **Scheduled Readings**: Configure when and how often to read the meter
- **Frequency Management**: Automatic frequency scanning and optimization
- **Comprehensive Monitoring**: Track signal quality, battery life, and reading statistics
- **Multiple Meter Types**: Supports both water and gas meters
- **Retry Logic**: Configurable retry attempts with cooldown periods
- **Low Power**: Efficient reading schedule minimizes power consumption

## 📖 Example Configurations

Three complete example configurations are provided:

1. **[Water Meter Example](../example-water-meter.yaml)** - Full-featured water meter configuration
2. **[Gas Meter Example](../example-gas-meter-minimal.yaml)** - Minimal gas meter configuration
3. **[Advanced Example](../example-advanced.yaml)** - Advanced features and customization

## 🔌 Hardware Requirements

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

⚠️ **Important**: The CC1101 requires 3.3V power. Do not connect to 5V!

## 🎯 Benefits

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

## 🛠️ Architecture

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

## 📦 Available Sensors

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

## 🔧 Common Configuration Patterns

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

## 🐛 Troubleshooting

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

For detailed troubleshooting, see the [Integration Guide](ESPHOME_INTEGRATION_GUIDE.md#troubleshooting).

## 📜 License

MIT License - See [LICENSE.md](../../LICENSE.md)

## 🙏 Credits

Based on the EverBlu Meters ESP8266 project with architectural improvements for reusability and ESPHome integration.

## 🔗 Links

- **Main Project**: [Main README](../../README.md)
- **GitHub Repository**: https://github.com/yourusername/everblu-meters-esp8266-improved
- **ESPHome Documentation**: https://esphome.io/
- **Home Assistant**: https://www.home-assistant.io/

---

**Need Help?**
- 📖 Start with the [Integration Guide](ESPHOME_INTEGRATION_GUIDE.md)
- 🔍 Check the [Quick Reference](QUICK_REFERENCE.md) for common tasks
- 🐛 See [Troubleshooting](ESPHOME_INTEGRATION_GUIDE.md#troubleshooting) for common issues
- 👨‍💻 Developers: See [Developer Guide](DEVELOPER_GUIDE.md) for architecture details
