# ESPHome Integration - Developer Guide

> **📚 For end-users**: See the complete [ESPHome Integration Guide](ESPHOME_INTEGRATION_GUIDE.md) for installation instructions, configuration, and troubleshooting.

This document contains technical architecture details for developers who want to understand or extend the ESPHome integration.

## Architecture Overview

The refactored codebase is designed for **maximum reusability** across different projects, especially ESPHome custom components. The key design principle is **dependency injection** - platform-specific implementations are injected rather than hard-coded.

## Why This Matters

The original 91JJ ESPHome fork had to make huge changes because the frequency management code was tightly coupled to specific implementations. This refactoring eliminates those barriers.

## Architecture

### Modular Components

The codebase is split into three independent layers:

1. **Storage Abstraction** (`storage_abstraction.h/cpp`)
   - Platform-independent persistent storage
   - Works on both ESP8266 (EEPROM) and ESP32 (Preferences)
   - No external dependencies

2. **Frequency Manager** (`frequency_manager.h/cpp`)
   - Frequency calibration and optimization logic
   - **Uses dependency injection** - no hard CC1101 dependencies
   - Can work with ANY CC1101 wrapper implementation

3. **Application Layer** (`main.cpp`)
   - MQTT, WiFi, Home Assistant integration
   - Injects CC1101 functions into FrequencyManager
   - Shows example usage pattern

### Key Feature: Dependency Injection

The `FrequencyManager` class does NOT directly call CC1101 functions. Instead, it accepts **function pointers** (callbacks) that you inject:

```cpp
// Function pointer types
typedef bool (*RadioInitCallback)(float freq);
typedef tmeter_data (*MeterReadCallback)();
typedef void (*StatusCallback)(const char *state, const char *message);

// Setter methods
FrequencyManager::setRadioInitCallback(your_radio_init_function);
FrequencyManager::setMeterReadCallback(your_meter_read_function);
```

This means:
- ✅ ESPHome can inject its own CC1101 wrapper methods
- ✅ Arduino projects can inject raw CC1101 calls
- ✅ Other frameworks can inject their own implementations
- ✅ **Same frequency logic works everywhere**

## ESPHome Integration Pattern

### Step 1: Copy Core Modules

Copy these files to your ESPHome custom component:

```
components/your_component/
├── storage_abstraction.h
├── storage_abstraction.cpp
├── frequency_manager.h
└── frequency_manager.cpp
```

These files have **no MQTT or Arduino-specific dependencies** (just Arduino.h for Serial, which ESPHome provides).

### Step 2: Create Your Component

```cpp
// your_component.h
#include "esphome/core/component.h"
#include "frequency_manager.h"
#include "storage_abstraction.h"

class YourComponent : public Component {
public:
    void setup() override;
    void loop() override;
    
    // Your CC1101 wrapper methods
    bool init_radio(float freq);
    tmeter_data read_meter();
    
private:
    // CC1101 instance (ESPHome's CC1101 component)
    CC1101 *cc1101_;
};
```

### Step 3: Inject Callbacks in setup()

```cpp
// your_component.cpp
void YourComponent::setup() {
    // Inject THIS component's methods into FrequencyManager
    FrequencyManager::setRadioInitCallback([this](float freq) {
        return this->init_radio(freq);
    });
    
    FrequencyManager::setMeterReadCallback([this]() {
        return this->read_meter();
    });
    
    // Now initialize - FrequencyManager will use YOUR functions
    FrequencyManager::begin(433.82f);
    
    // Optionally perform frequency scan
    if (FrequencyManager::shouldPerformAutoScan()) {
        auto status_cb = [](const char *state, const char *msg) {
            ESP_LOGI("freq", "Status: %s - %s", state, msg);
        };
        FrequencyManager::performWideInitialScan(status_cb);
    }
}

bool YourComponent::init_radio(float freq) {
    // Use ESPHome's CC1101 component methods here
    this->cc1101_->set_frequency(freq);
    this->cc1101_->begin();
    return this->cc1101_->is_connected();
}

tmeter_data YourComponent::read_meter() {
    // Call your meter reading logic using ESPHome's CC1101
    tmeter_data result;
    // ... your implementation ...
    return result;
}
```

### Step 4: Use FrequencyManager Features

```cpp
void YourComponent::loop() {
    // Get current optimal frequency
    float freq = FrequencyManager::getTunedFrequency();
    
    // After successful meter read, adapt frequency
    if (meter_read_successful) {
        FrequencyManager::adaptiveFrequencyTracking(freqest_value);
    }
    
    // Manual frequency scan if needed
    if (should_scan) {
        FrequencyManager::performFrequencyScan(nullptr);
    }
}
```

## Complete Example: ESPHome Custom Component

### esphome_meter.h
```cpp
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "frequency_manager.h"
#include "storage_abstraction.h"

namespace esphome {
namespace everblu_meter {

class EverbluMeter : public PollingComponent {
public:
    void setup() override;
    void update() override;
    
    void set_frequency(float freq) { base_frequency_ = freq; }
    void set_cc1101(/* ESPHome CC1101 component */) { /* ... */ }
    
protected:
    bool init_radio_at_frequency(float freq);
    tmeter_data read_meter_data();
    
    float base_frequency_{433.82f};
    // CC1101 reference...
};

} // namespace everblu_meter
} // namespace esphome
```

### esphome_meter.cpp
```cpp
#include "esphome_meter.h"
#include "esphome/core/log.h"

namespace esphome {
namespace everblu_meter {

static const char *TAG = "everblu_meter";

void EverbluMeter::setup() {
    ESP_LOGI(TAG, "Setting up EverBlu meter component...");
    
    // Inject our methods into FrequencyManager using lambdas
    FrequencyManager::setRadioInitCallback([this](float freq) -> bool {
        return this->init_radio_at_frequency(freq);
    });
    
    FrequencyManager::setMeterReadCallback([this]() -> tmeter_data {
        return this->read_meter_data();
    });
    
    // Initialize frequency management
    float stored_offset = FrequencyManager::begin(this->base_frequency_);
    ESP_LOGI(TAG, "Frequency offset loaded: %.6f MHz", stored_offset);
    
    // Perform initial scan if no stored offset
    if (FrequencyManager::shouldPerformAutoScan()) {
        ESP_LOGW(TAG, "No frequency offset found - performing initial scan...");
        
        auto status = [](const char *state, const char *msg) {
            ESP_LOGI(TAG, "[SCAN] %s: %s", state, msg);
        };
        
        FrequencyManager::performWideInitialScan(status);
        ESP_LOGI(TAG, "Initial scan complete!");
    }
}

void EverbluMeter::update() {
    // Called every polling interval
    float freq = FrequencyManager::getTunedFrequency();
    
    // Initialize radio at optimal frequency
    if (!init_radio_at_frequency(freq)) {
        ESP_LOGE(TAG, "Failed to initialize radio at %.6f MHz", freq);
        return;
    }
    
    // Read meter data
    tmeter_data data = read_meter_data();
    
    if (data.reads_counter > 0) {
        ESP_LOGI(TAG, "Meter read successful - RSSI: %d dBm", data.rssi_dbm);
        
        // Adaptive tracking: let FrequencyManager optimize frequency
        FrequencyManager::adaptiveFrequencyTracking(data.freqest);
        
        // Publish to sensors...
    } else {
        ESP_LOGW(TAG, "No meter data received");
    }
}

bool EverbluMeter::init_radio_at_frequency(float freq) {
    // Use ESPHome's CC1101 component to initialize radio
    ESP_LOGD(TAG, "Initializing CC1101 at %.6f MHz", freq);
    
    // Example (adapt to your CC1101 component):
    // this->cc1101_->set_frequency(freq);
    // this->cc1101_->set_modulation(...);
    // return this->cc1101_->begin();
    
    return true; // Replace with actual implementation
}

tmeter_data EverbluMeter::read_meter_data() {
    tmeter_data result = {};
    
    // Your meter reading logic here using ESPHome's CC1101
    // Example:
    // this->cc1101_->receive_mode();
    // ... wait for data ...
    // result.reads_counter = 1;
    // result.rssi_dbm = this->cc1101_->get_rssi();
    
    return result;
}

} // namespace everblu_meter
} // namespace esphome
```

### YAML Configuration
```yaml
external_components:
  - source: github://yourusername/everblu-esphome
    components: [ everblu_meter ]

everblu_meter:
  frequency: 433.82  # Base meter frequency in MHz
  
sensor:
  - platform: everblu_meter
    name: "Water Consumption"
    update_interval: 60s
```

## Benefits of This Approach

### ✅ No Code Duplication
- Core frequency logic stays in one place
- Bug fixes benefit all projects
- Improvements are portable

### ✅ Clean Separation
- Storage abstraction handles platform differences
- Frequency manager handles calibration logic
- Your component handles CC1101 specifics

### ✅ Easy Testing
- Can inject mock functions for unit tests
- Can test frequency logic without hardware
- Can validate algorithms independently

### ✅ Framework Agnostic
- Works with Arduino (current main.cpp)
- Works with ESPHome (shown above)
- Works with PlatformIO, ESP-IDF, etc.

## Migration from 91JJ Fork

If you're currently using the 91JJ ESPHome fork:

1. **Keep your CC1101 component** - no need to change it
2. **Copy the new modules** - storage_abstraction, frequency_manager
3. **Inject your CC1101 methods** - use the callback pattern shown above
4. **Remove old frequency code** - the new modules handle it all

### Before (91JJ Pattern)
```cpp
// Had to rewrite everything in cc1101_component
void CC1101Component::perform_frequency_scan() {
    // Duplicated frequency logic here
    // Tightly coupled to ESPHome
}
```

### After (New Pattern)
```cpp
// Reuse existing FrequencyManager
FrequencyManager::setRadioInitCallback([this](float f) {
    return this->cc1101_init(f);
});
FrequencyManager::performFrequencyScan(nullptr);
```

## Advanced: Customizing Frequency Behavior

### Disable Auto-Scan
```cpp
FrequencyManager::setAutoScanEnabled(false);
```

### Adjust Adaptive Threshold
```cpp
// Adapt after 20 reads instead of default 10
FrequencyManager::setAdaptiveThreshold(20);
```

### Manual Frequency Scan
```cpp
// With status updates
auto status = [](const char *state, const char *msg) {
    ESP_LOGI(TAG, "%s: %s", state, msg);
};
FrequencyManager::performFrequencyScan(status);

// Without status updates
FrequencyManager::performFrequencyScan(nullptr);
```

### Direct Offset Control
```cpp
// Get current offset
float offset = FrequencyManager::getOffset();

// Set new offset
FrequencyManager::setOffset(-0.005f);  // -5 kHz

// Save to storage
FrequencyManager::saveFrequencyOffset(offset);
```

## Storage Behavior

The `StorageAbstraction` module automatically handles:
- **ESP8266**: Uses EEPROM with automatic commit
- **ESP32**: Uses Preferences with namespace "everblu"
- **Validation**: Magic number (0xABCD) prevents reading garbage
- **Range checking**: Rejects offsets outside ±100 kHz

Storage keys:
- `freq_offset`: Frequency offset in MHz (validated)

## Testing Your Integration

### Verify Callbacks Are Set
```cpp
// FrequencyManager will print errors if callbacks not set
FrequencyManager::begin(433.82);
// Should log: "> FrequencyManager: Radio init callback registered"
//             "> FrequencyManager: Meter read callback registered"
//             "> FrequencyManager initialized: base=433.820000 MHz, offset=0.000000 MHz"
```

### Test Frequency Scan
```cpp
// Run a narrow scan (±30 kHz)
FrequencyManager::performFrequencyScan([](const char *s, const char *m) {
    ESP_LOGI(TAG, "Scan: %s - %s", s, m);
});
```

### Test Adaptive Tracking
```cpp
// After each successful meter read:
FrequencyManager::adaptiveFrequencyTracking(freqest_register_value);
// Watch logs for automatic frequency adjustments
```

## Troubleshooting

### "ERROR: Radio init callback not set"
- Call `FrequencyManager::setRadioInitCallback()` **before** `begin()`

### "ERROR: Meter read callback not set"
- Call `FrequencyManager::setMeterReadCallback()` **before** `begin()`

### Frequency scans fail
- Verify your `RadioInitCallback` returns `true` on success
- Check your CC1101 is properly initialized
- Ensure antenna is connected

### Adaptive tracking not working
- Verify `freqest` register is being read from CC1101
- Check `freqest` value is not always 0
- Increase adaptive threshold if reads are noisy

## Summary

This refactored architecture provides **maximum flexibility** for ESPHome integration while maintaining all the sophisticated frequency management features. By using dependency injection, you can:

1. Use ANY CC1101 wrapper (ESPHome, Arduino, custom)
2. Keep all frequency logic in one tested module
3. Avoid code duplication across projects
4. Easily port to new platforms

The key is the callback pattern - your code tells FrequencyManager "use MY functions" rather than FrequencyManager dictating which functions you must have.
