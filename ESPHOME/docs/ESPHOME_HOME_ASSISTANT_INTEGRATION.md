# ESPHome Integration with Home Assistant

This guide explains how to access meter data in Home Assistant and set up reliable long-term tracking.

## Overview

The ESPHome component publishes meter data through two primary sensors:
- **`volume`** sensor: Current meter reading (numeric value)
- **`history`** text sensor: JSON containing historical data

## Accessing Historical Data

The `history` text sensor contains a JSON object with monthly readings:

```json
{
  "history": [605696, 614107, 621401, 630219, 640054, 652789, 667441, 684214, 700917, 712720, 721549, 728836],
  "monthly_usage": [605696, 8411, 7294, 8818, 9835, 12735, 14652, 16773, 16703, 11803, 8829, 7287],
  "current_month_usage": 13038,
  "months_available": 12
}
```

### Creating Template Sensors from History

You can extract specific values in Home Assistant's `configuration.yaml`:

```yaml
template:
  - sensor:
      # Extract current month usage
      - name: "Water Meter Current Month Usage"
        unique_id: everblu_current_month_usage
        unit_of_measurement: "L"
        device_class: water
        state_class: total_increasing
        state: >
          {% set history = states('sensor.everblu_meter_history') | from_json %}
          {{ history.current_month_usage if history else 0 }}
        availability: >
          {{ states('sensor.everblu_meter_history') not in ['unavailable', 'unknown', 'none'] }}

      # Extract last month's total
      - name: "Water Meter Last Month Total"
        unique_id: everblu_last_month_total
        unit_of_measurement: "L"
        device_class: water
        state: >
          {% set history = states('sensor.everblu_meter_history') | from_json %}
          {{ history.history[-1] if history and history.history|length > 0 else 0 }}

      # Calculate last month's usage
      - name: "Water Meter Last Month Usage"
        unique_id: everblu_last_month_usage
        unit_of_measurement: "L"
        device_class: water
        state: >
          {% set history = states('sensor.everblu_meter_history') | from_json %}
          {{ history.monthly_usage[-1] if history and history.monthly_usage|length > 0 else 0 }}
```

## Setting Up a Utility Meter Helper (Recommended)

**Important**: Always create a Home Assistant **Utility Meter** helper to track your water/gas consumption. This provides several critical benefits:

### Why Use a Utility Meter Helper?

1. **Persistent tracking**: Meter readings remain available even when the ESP is offline/rebooting
2. **Hardware independence**: Change ESPs, meter modules, or serial numbers without losing history
3. **Master record**: Home Assistant becomes the source of truth, not the hardware
4. **Long-term statistics**: Home Assistant's database tracks all changes permanently
5. **Tariff support**: Split consumption by time periods (daily, weekly, monthly, yearly)

### Step-by-Step Setup

#### 1. Create Input Number Helper (Optional but Recommended)

First, create an input number to calibrate if needed:

**UI Method:**
1. Go to **Settings** → **Devices & Services** → **Helpers**
2. Click **+ Create Helper** → **Number**
3. Configure:
   - **Name**: `Water Meter Calibration Offset`
   - **Minimum**: `0`
   - **Maximum**: `1000000`
   - **Step**: `1`
   - **Unit**: `L`

**YAML Method** (in `configuration.yaml`):
```yaml
input_number:
  water_meter_calibration_offset:
    name: Water Meter Calibration Offset
    min: 0
    max: 1000000
    step: 1
    unit_of_measurement: "L"
    mode: box
```

#### 2. Create Template Sensor with Calibration

This sensor applies any offset and serves as the source for the utility meter:

```yaml
template:
  - sensor:
      - name: "Water Meter Calibrated"
        unique_id: everblu_water_meter_calibrated
        unit_of_measurement: "L"
        device_class: water
        state_class: total_increasing
        state: >
          {% set raw = states('sensor.everblu_meter_volume') | float(0) %}
          {% set offset = states('input_number.water_meter_calibration_offset') | float(0) %}
          {{ (raw + offset) | round(0) }}
        availability: >
          {{ states('sensor.everblu_meter_volume') not in ['unavailable', 'unknown', 'none'] }}
```

#### 3. Create Utility Meter Helper

**UI Method:**
1. Go to **Settings** → **Devices & Services** → **Helpers**
2. Click **+ Create Helper** → **Utility Meter**
3. Configure:
   - **Name**: `Water Consumption`
   - **Input sensor**: `sensor.water_meter_calibrated` (or `sensor.everblu_meter_volume`)
   - **Meter reset**: Choose your billing cycle (Daily, Weekly, Monthly, Yearly, or Never)
   - **Tariffs**: Leave empty unless you have time-based pricing

**YAML Method** (in `configuration.yaml`):
```yaml
utility_meter:
  water_consumption_daily:
    source: sensor.water_meter_calibrated
    cycle: daily
    
  water_consumption_monthly:
    source: sensor.water_meter_calibrated
    cycle: monthly
    
  water_consumption_yearly:
    source: sensor.water_meter_calibrated
    cycle: yearly
```

#### 4. Add to Dashboard

The utility meters will appear as sensors:
- `sensor.water_consumption_daily`
- `sensor.water_consumption_monthly`
- `sensor.water_consumption_yearly`

Add them to your dashboard with statistics cards showing trends over time.

### Handling Hardware Changes

When you need to replace the ESP or change meter configuration:

1. **Note the current reading** from Home Assistant's utility meter (not the ESP)
2. Replace/reconfigure the ESP hardware
3. **Calibrate the offset**:
   - Old HA reading: 725,000 L
   - New ESP reading: 728,836 L (the actual meter value)
   - You want HA to continue from 725,000 L
   - Set calibration offset: `725000 - 728836 = -3836`
   
   Alternatively, if you want to sync HA to the physical meter:
   - Just use the new reading directly (no offset needed)
   - Home Assistant will continue tracking from the new value

4. The utility meter **automatically continues** tracking without losing history

### Example: Complete Configuration

```yaml
# configuration.yaml

# Optional: Calibration offset
input_number:
  water_meter_calibration_offset:
    name: Water Meter Calibration Offset
    min: -1000000
    max: 1000000
    step: 1
    unit_of_measurement: "L"
    mode: box

# Template sensor (source of truth)
template:
  - sensor:
      # Main meter with calibration
      - name: "Water Meter Calibrated"
        unique_id: everblu_water_meter_calibrated
        unit_of_measurement: "L"
        device_class: water
        state_class: total_increasing
        state: >
          {% set raw = states('sensor.everblu_meter_volume') | float(0) %}
          {% set offset = states('input_number.water_meter_calibration_offset') | float(0) %}
          {{ (raw + offset) | round(0) }}
        availability: >
          {{ states('sensor.everblu_meter_volume') not in ['unavailable', 'unknown', 'none'] }}

      # Extract historical data
      - name: "Water Meter Current Month Usage"
        unique_id: everblu_current_month_usage
        unit_of_measurement: "L"
        device_class: water
        state: >
          {% set history = states('sensor.everblu_meter_history') | from_json %}
          {{ history.current_month_usage if history else 0 }}
        availability: >
          {{ states('sensor.everblu_meter_history') not in ['unavailable', 'unknown', 'none'] }}

# Utility meters for consumption tracking
utility_meter:
  water_consumption_daily:
    source: sensor.water_meter_calibrated
    name: Daily Water Consumption
    cycle: daily
    
  water_consumption_weekly:
    source: sensor.water_meter_calibrated
    name: Weekly Water Consumption
    cycle: weekly
    
  water_consumption_monthly:
    source: sensor.water_meter_calibrated
    name: Monthly Water Consumption
    cycle: monthly
    
  water_consumption_yearly:
    source: sensor.water_meter_calibrated
    name: Yearly Water Consumption
    cycle: yearly
```

### Dashboard Card Example

```yaml
type: vertical-stack
cards:
  # Current reading
  - type: sensor
    entity: sensor.water_meter_calibrated
    name: Current Meter Reading
    graph: line
    hours_to_show: 168
    
  # Consumption periods
  - type: entities
    title: Water Consumption
    entities:
      - entity: sensor.water_consumption_daily
        name: Today
      - entity: sensor.water_consumption_weekly
        name: This Week
      - entity: sensor.water_consumption_monthly
        name: This Month
      - entity: sensor.water_consumption_yearly
        name: This Year
      - type: divider
      - entity: sensor.water_meter_current_month_usage
        name: Current Month (from meter)
      - type: divider
      - entity: input_number.water_meter_calibration_offset
        name: Calibration Offset
  
  # Statistics card showing long-term trends
  - type: statistics-graph
    title: Monthly Water Usage History
    entities:
      - sensor.water_consumption_monthly
    stat_types:
      - sum
    period:
      calendar:
        period: month
```

## Gas Meters

For gas meters, replace:
- `unit_of_measurement: "L"` → `unit_of_measurement: "m³"`
- `device_class: water` → `device_class: gas`

The same principles and setup process apply.

## Troubleshooting

### History Sensor Shows "unavailable"

- Check that the ESP has successfully read the meter at least once
- Historical data is only available if the meter has stored monthly readings
- The meter must have been in operation for at least one complete month

### Utility Meter Not Updating

- Verify the source sensor has `state_class: total_increasing`
- Check that the source sensor is publishing values
- Ensure the source sensor never decreases (use calibration offset if needed)

### After ESP Replacement

1. Old meter reading: Record from Home Assistant
2. New ESP reading: Note the physical meter value
3. Calculate offset: `old_ha_reading - new_esp_reading`
4. Set `input_number.water_meter_calibration_offset` to the calculated offset
5. The utility meter continues seamlessly from the old value

## Benefits Summary

| Without Utility Meter | With Utility Meter |
|----------------------|-------------------|
| Data lost if ESP offline | Always available |
| History lost on hardware change | Continuous history |
| No consumption cycles | Daily/Weekly/Monthly/Yearly tracking |
| ESP is master record | Home Assistant is master record |
| Limited long-term statistics | Full database history |

**Best Practice**: Always use a Home Assistant Utility Meter helper for production deployments. It provides resilience, flexibility, and superior long-term data management.

## Additional Resources

- [Home Assistant Utility Meter Integration](https://www.home-assistant.io/integrations/utility_meter/)
- [Home Assistant Template Sensors](https://www.home-assistant.io/integrations/template/)
- [Home Assistant Helpers](https://www.home-assistant.io/integrations/input_number/)
