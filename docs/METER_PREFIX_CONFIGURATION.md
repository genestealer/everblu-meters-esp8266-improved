# Meter Prefix Configuration Guide

## Overview

This feature allows you to control whether the meter serial number is included as a prefix in MQTT entity IDs and Home Assistant entity names. This is essential for users running a single meter who want to preserve their existing Home Assistant history without interruption.

**Issue Reference:** [#48 - Make meter number prefix optional](https://github.com/psykokwak-com/everblu-meters-esp8266-improved/issues/48)

## Problem Statement

When running a single meter for an extended period, the firmware previously always prefixed MQTT entity IDs with the meter serial number. For example:
- With meter serial `257750`, entities would be named: `257750_everblu_meter_value`, `257750_everblu_meter_counter`, etc.

Upgrading to a new firmware version would change the entity ID format, causing Home Assistant to lose all historical data associated with those entities, even though it's the same meter.

## Solution

A new configuration option `ENABLE_METER_PREFIX_IN_ENTITY_IDS` allows you to disable the meter serial prefix for single-meter setups while maintaining backward compatibility for multi-meter deployments.

## Configuration

### Option: `ENABLE_METER_PREFIX_IN_ENTITY_IDS`

Edit your `private.h` configuration file:

```cpp
// Meter number prefix in entity IDs
// Controls whether the meter serial number is included as a prefix in MQTT entity IDs
// and Home Assistant entity names. This is useful for distinguishing entities when
// running multiple meters on the same broker.
//
// 1 (default): Include meter serial as prefix (e.g., "257750_everblu_meter_value")
//              Use this for multiple meters or to preserve multi-meter MQTT history
// 0:           Omit meter serial prefix (e.g., "everblu_meter_value")
//              Use this for single meters where you want to keep existing Home Assistant history
//
#define ENABLE_METER_PREFIX_IN_ENTITY_IDS 1
```

### Default Behavior

- **Default Value:** `1` (enabled)
- **Backward Compatible:** Existing configurations automatically use the default (prefix enabled)
- **No Change Required:** Multi-meter setups continue to work without any configuration changes

## Usage Scenarios

### Scenario 1: Single Meter Setup (Preserve History)

If you have a single meter and want to keep your Home Assistant history intact:

```cpp
#define ENABLE_METER_PREFIX_IN_ENTITY_IDS 0
```

**Entity ID Format:**
- `everblu_meter_value` (instead of `257750_everblu_meter_value`)
- `everblu_meter_counter` (instead of `257750_everblu_meter_counter`)
- `everblu_meter_battery` (instead of `257750_everblu_meter_battery`)

**Advantages:**
- ✅ Existing Home Assistant entities and history remain intact
- ✅ No need to reconfigure dashboards or automations
- ✅ Cleaner entity names for single-meter setups

### Scenario 2: Multi-Meter Setup (Multiple Devices)

If you have multiple EverBlu meters on the same MQTT broker:

```cpp
#define ENABLE_METER_PREFIX_IN_ENTITY_IDS 1  // Keep as default
```

**Entity ID Format per meter:**
- Meter `257750`: `257750_everblu_meter_value`, `257750_everblu_meter_counter`, etc.
- Meter `2777550`: `2777550_everblu_meter_value`, `2777550_everblu_meter_counter`, etc.

**Advantages:**
- ✅ Each meter's entities are uniquely identifiable
- ✅ No MQTT topic conflicts
- ✅ Home Assistant distinguishes entities for different meters
- ✅ Proper device grouping in Home Assistant

## What Changes When Prefix is Disabled

### MQTT Topics

MQTT **topic names** ARE affected by this setting:
- With prefix disabled: Uses `everblu/cyble` base topic (meter serial NOT in topic path)
- With prefix enabled: Uses `everblu/cyble/{METER_SERIAL}` base topic

### Home Assistant Entity IDs

Entity IDs are affected - the prefix is removed from the entity identifier:

**With prefix enabled (`ENABLE_METER_PREFIX_IN_ENTITY_IDS = 1`):**
```
uniq_id: 257750_everblu_meter_value
obj_id: 257750_everblu_meter_value
```

**With prefix disabled (`ENABLE_METER_PREFIX_IN_ENTITY_IDS = 0`):**
```
uniq_id: everblu_meter_value
obj_id: everblu_meter_value
```

### Device Identification

**With prefix enabled:**
```json
{
  "ids": ["257750"],
  "name": "EverBlu Meter 257750"
}
```

**With prefix disabled:**
```json
{
  "ids": ["everblu_meter_device"],
  "name": "EverBlu Meter"
}
```

## Migration Guide

### From v1.15 to v2.0 (With Existing History)

If you had v1.15 running with a single meter and want to preserve your Home Assistant history:

1. **Locate your v1.15 entity IDs** - They should look like `257750_water_meter` or similar
2. **Set in private.h:**
   ```cpp
   #define ENABLE_METER_PREFIX_IN_ENTITY_IDS 0
   ```
3. **Compile and upload** the firmware
4. **Verify in Home Assistant:** Your existing entities should continue to work with the same data

### From v1.15 to v2.0 (Multi-Meter Setup)

If you're running multiple meters:

1. **Leave default setting:**
   ```cpp
   #define ENABLE_METER_PREFIX_IN_ENTITY_IDS 1
   ```
2. **Compile and upload** for each device
3. **Each meter will get properly prefixed entities** with its unique serial number

## Home Assistant Integration

### Single-Meter Setup (Prefix Disabled)

In your `automations.yaml` or templates, you'd use entities like:
```yaml
entity_id: sensor.everblu_meter_value
entity_id: sensor.everblu_meter_battery
entity_id: binary_sensor.everblu_meter_active_reading
```

### Multi-Meter Setup (Prefix Enabled)

For multiple meters, use the prefixed entity IDs:
```yaml
# Meter 257750
entity_id: sensor.257750_everblu_meter_value
entity_id: sensor.257750_everblu_meter_battery

# Meter 2777550
entity_id: sensor.2777550_everblu_meter_value
entity_id: sensor.2777550_everblu_meter_battery
```

## Troubleshooting

### I disabled the prefix but Home Assistant shows duplicate entities

**Solution:** Clear your Home Assistant's MQTT discovery cache:
1. In Home Assistant, go to Settings → Devices & Services → MQTT
2. Click the "Publish" button and publish to topic: `homeassistant/+/+/config`
3. Restart the EverBlu device to republish discovery messages

### Entities disappeared after enabling/disabling the prefix

**Solution:** This is expected behavior due to entity ID changes:
1. The old entities are archived by Home Assistant
2. New entities are created with the new ID format
3. Your history remains with the old entity IDs
4. To view historical data, you may need to use the entity history panel

### Multi-meter setup shows conflicting entity names or data corruption

**Cause:** You have multiple devices with prefix disabled, causing MQTT topic collisions

**Critical Issue:** When the prefix is disabled on multiple meters, all devices publish to the **exact same MQTT topics** (e.g., `everblu/cyble/liters`). This causes:
- **Data corruption:** Each meter overwrites the other meter's data
- **Conflicting entity IDs:** Home Assistant cannot distinguish between meters
- **Unreliable readings:** The last meter to publish wins, so readings are inconsistent

**Solution:** 
- Set all devices to: `#define ENABLE_METER_PREFIX_IN_ENTITY_IDS 1`
- Restart all devices
- Home Assistant will discover them with unique prefixed entity IDs
- Each meter will use unique MQTT topics (e.g., `everblu/cyble/257750/liters`, `everblu/cyble/2777550/liters`)

## Technical Details

### Source Code Changes

- **Configuration Option:** Defined in `private.example.h` and `private.h`
- **Helper Function:** `getMeterPrefix()` in `src/main.cpp` returns the prefix conditionally
- **Affected Functions:**
  - `buildDeviceJson()` - Device identifier in Home Assistant
  - `buildDiscoveryJson()` - Entity ID generation for sensors
  - `publishDiscoveryMessage()` - Discovery topic path
  - `publishHADiscovery()` - All manual discovery JSON generation
  - Topic initialization - `mqttBaseTopic` and `mqttLwtTopic`

### Backward Compatibility

- Default value of `1` ensures existing configurations work unchanged
- Code uses compile-time conditionals (`#if ENABLE_METER_PREFIX_IN_ENTITY_IDS`)
- No runtime overhead when feature is not needed

## FAQs

**Q: Will this work with the ESPHome integration?**
A: Yes, the ESPHome release uses the same configuration mechanism.

**Q: Can I change this setting after deployment?**
A: Yes, but Home Assistant will see it as new entities. The old entities' history is preserved but associated with the old entity ID.

**Q: Is there any performance impact?**
A: No, the prefix is determined at compile-time and has zero runtime overhead.

**Q: What happens to my MQTT topics?**
A: The base MQTT topic is affected by this setting. When the prefix is disabled, the base topic is `everblu/cyble`. When the prefix is enabled, the base topic becomes `everblu/cyble/{METER_SERIAL}`. Entity IDs and Home Assistant entity names follow the same prefix rule.

**Q: Can I use this for meter replacement?**
A: Yes! If you replace your meter with a new one and want to preserve history, keep the prefix disabled and the new meter will use the same entity IDs.

## Related Issues

- **Issue #48:** Make meter number prefix optional
- **Release:** v2.0+

## Support

For issues or questions, please refer to the main repository or create an issue with the tag `meter-prefix`.
