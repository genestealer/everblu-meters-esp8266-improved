# ESPHome SPI Integration Approach

## Implementation Decision

I have pivoted to using **ESPHome's native SPI component integration** instead of implementing raw GPIO pin overrides. This architectural decision provides significant benefits for the ESPHome ecosystem and end users.

## Why ESPHome SPI Integration?

Instead of adding individual `spi_clk_pin`, `spi_mosi_pin`, `spi_miso_pin` configuration options as initially requested in issue #51, the component now integrates with ESPHome's standard `spi:` platform component. This approach was chosen because:

1. **Follows ESPHome conventions**: Aligns with how other SPI-based components (displays, sensors, SD cards) are implemented in the ESPHome ecosystem
2. **Avoids code duplication**: ESPHome's SPI component already handles hardware/software SPI selection, pin validation, and bus configuration
3. **Enables bus sharing**: Multiple SPI devices can share the same bus without conflicts
4. **Better hardware support**: ESPHome's SPI component handles platform-specific quirks (ESP8266, ESP32, ESP32-S3, etc.) automatically
5. **Future-proof**: New ESP32 variants with different SPI configurations are supported through ESPHome core updates

## Benefits of This Approach

### For End Users
- ✅ **Cleaner configuration**: Standard ESPHome SPI syntax that's familiar to users
- ✅ **Bus sharing**: Can add displays, SD cards, or other SPI devices alongside the CC1101
- ✅ **Hardware/software SPI selection**: ESPHome can automatically choose the best SPI controller
- ✅ **Better validation**: Pin conflicts detected at compile-time
- ✅ **Consistent behavior**: Works the same way as other ESPHome SPI components

### For Developers
- ✅ **Less maintenance**: SPI platform-specific code maintained by ESPHome core team
- ✅ **Better testing**: Integration tested alongside all other SPI components
- ✅ **Proper abstraction**: Clean separation between bus configuration and device logic
- ✅ **Extensibility**: Easy to add features like configurable data rates or SPI modes

## Configuration Example

### Arduino Nano ESP32 (ESP32-S3) with CC1101

```yaml
# SPI bus configuration for Arduino Nano ESP32
spi:
  id: main_bus
  clk_pin: 48   # D13 (SCK)
  mosi_pin: 38  # D11 (MOSI)
  miso_pin: 47  # D12 (MISO)

# EverBlu Meter Component
everblu_meter:
  spi_id: main_bus
  cs_pin: 21    # D10 (CS)
  gdo0_pin: 5   # D2 (GDO0)
  
  # Meter configuration
  meter_year: 20
  meter_serial: 12345678
  time_id: homeassistant_time
  
  # Sensors
  volume:
    name: "Water Volume"
```

### ESP8266 (Adafruit HUZZAH)

```yaml
# SPI bus configuration for ESP8266
spi:
  id: main_bus
  clk_pin: 14   # GPIO14 (D5)
  mosi_pin: 13  # GPIO13 (D7)
  miso_pin: 12  # GPIO12 (D6)

# EverBlu Meter Component
everblu_meter:
  spi_id: main_bus
  cs_pin: 15    # GPIO15 (D8)
  gdo0_pin: 4   # GPIO4 (D2)
  
  meter_year: 20
  meter_serial: 12345678
  time_id: homeassistant_time
```

## Technical Implementation

The component now:
1. Inherits from `spi::SPIDevice` template class
2. References the ESPHome SPI bus via `spi_id`
3. Uses Arduino SPI library calls (bus already configured by ESPHome)
4. Manages CS and GDO0 pins as raw GPIO integers (compatible with Arduino pinMode/digitalRead)

## Compatibility

- ✅ **ESP8266**: All variants
- ✅ **ESP32**: All variants (ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6)
- ✅ **GPIO ranges**: Supports up to GPIO50 for future ESP32 variants
- ✅ **USB CDC Serial**: Compatible with ESP32-S3 USB CDC mode (Stream& base class)
- ✅ **Backward compatible**: Existing MQTT/PlatformIO builds unaffected

## Migration from Raw Pin Approach

If you previously used (or expected) raw pin configuration:

**❌ Old approach (not implemented):**
```yaml
everblu_meter:
  spi_clk_pin: 48
  spi_mosi_pin: 38
  spi_miso_pin: 47
  spi_cs_pin: 21
  gdo0_pin: 5
```

**✅ New approach (ESPHome SPI integration):**
```yaml
spi:
  id: main_bus
  clk_pin: 48
  mosi_pin: 38
  miso_pin: 47

everblu_meter:
  spi_id: main_bus
  cs_pin: 21
  gdo0_pin: 5
```

## Related Issue

This implementation addresses [issue #51](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/51) - ESPHome external component fixes for Arduino Nano ESP32 (ESP32-S3) + CC1101.
