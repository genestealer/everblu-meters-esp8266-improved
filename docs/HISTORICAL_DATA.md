# Historical Data Feature

## Overview

The Itron EverBlu Cyble Enhanced water meter internally stores 13 months of historical volume readings. While the basic RADIAN protocol doesn't provide the exact timestamps for these readings, it does transmit the volume values themselves as part of the meter's response payload.

This implementation extracts these 13 monthly historical readings and publishes them as JSON attributes in Home Assistant, making them accessible for advanced analytics, graphing, and automation.

## Data Structure

### Meter Payload
The historical data is located in the meter's response payload at bytes [66-117]:
- **13 consecutive uint32_t values** (4 bytes each, LSB first)
- Each value represents the **total cumulative volume** at the end of that month
- Index 0 = oldest month (13 months ago)
- Index 12 = most recent complete month

### Example from Real Meter Data
```
Current reading: 721,390 liters (October 30, 2025)

Historical readings (bytes 66-117):
[66-69]:   583,606 liters  (End of Sept 2024)
[70-73]:   590,305 liters  (End of Oct 2024)
[74-77]:   597,686 liters  (End of Nov 2024)
[78-81]:   605,696 liters  (End of Dec 2024)
[82-85]:   614,107 liters  (End of Jan 2025)
[86-89]:   621,401 liters  (End of Feb 2025)
[90-93]:   629,195 liters  (End of Mar 2025)
[94-97]:   639,030 liters  (End of Apr 2025)
[98-101]:  652,789 liters  (End of May 2025)
[102-105]: 667,441 liters  (End of Jun 2025)
[106-109]: 684,214 liters  (End of Jul 2025)
[110-113]: 700,917 liters  (End of Aug 2025)
[114-117]: 713,744 liters  (End of Sept 2025)
```

## Home Assistant Integration

### MQTT Topics

#### State Topic
- **Topic**: `everblu/cyble/liters`
- **Value**: Current total volume reading (e.g., `721390`)

#### Attributes Topic
- **Topic**: `everblu/cyble/liters_attributes`
- **Format**: JSON object with historical data

### JSON Attributes Structure

```json
{
  "history": [
    583606,  // Month -13 (oldest)
    590305,  // Month -12
    597686,  // Month -11
    605696,  // Month -10
    614107,  // Month -9
    621401,  // Month -8
    629195,  // Month -7
    639030,  // Month -6
    652789,  // Month -5
    667441,  // Month -4
    684214,  // Month -3
    700917,  // Month -2
    713744   // Month -1 (most recent complete month)
  ],
  "monthly_usage": [
    0,       // Unknown (no baseline for oldest month)
    6699,    // Month -12 usage
    7381,    // Month -11 usage
    8010,    // Month -10 usage
    8411,    // Month -9 usage
    7294,    // Month -8 usage
    7794,    // Month -7 usage
    9835,    // Month -6 usage
    13759,   // Month -5 usage
    14652,   // Month -4 usage
    16773,   // Month -3 usage
    16703,   // Month -2 usage
    12827    // Month -1 usage
  ],
  "current_month_usage": 7646,
  "months_available": 13
}
```

### Accessing in Home Assistant

#### 1. View in Developer Tools
Navigate to **Developer Tools > States** and find the `sensor.water_meter_value` entity. The attributes will be displayed in the "Attributes" section.

#### 2. Use in Templates
Access historical data in automations and templates:

```yaml
# Get the most recent monthly boundary reading
{{ state_attr('sensor.water_meter_value', 'history')[12] }}

# Get current month's usage so far
{{ state_attr('sensor.water_meter_value', 'current_month_usage') }}

# Get previous month's total usage
{{ state_attr('sensor.water_meter_value', 'monthly_usage')[12] }}

# Calculate average monthly consumption over last 12 months (excluding oldest which has no baseline)
{% set monthly = state_attr('sensor.water_meter_value', 'monthly_usage')[1:] %}
{{ (monthly | sum / monthly | length) | round(0) }}
```

#### 3. Create Template Sensors

Add to your `configuration.yaml`:

```yaml
template:
  - sensor:
      - name: "Water Usage Current Month"
        unique_id: water_usage_current_month
        unit_of_measurement: "L"
        state: >
          {{ state_attr('sensor.water_meter_value', 'current_month_usage') | int(0) }}
        icon: mdi:water-pump
        
      - name: "Water Usage Last Month"
        unique_id: water_usage_last_month
        unit_of_measurement: "L"
        state: >
          {% set monthly = state_attr('sensor.water_meter_value', 'monthly_usage') %}
          {{ monthly[12] | int(0) if monthly else 0 }}
        icon: mdi:water-pump
        
      - name: "Water Usage Average (12 months)"
        unique_id: water_usage_average_12mo
        unit_of_measurement: "L"
        state: >
          {% set monthly = state_attr('sensor.water_meter_value', 'monthly_usage') %}
          {% if monthly %}
            {% set valid_months = monthly[1:] %}
            {{ (valid_months | sum / valid_months | length) | round(0) }}
          {% else %}
            0
          {% endif %}
        icon: mdi:water-pump
```

#### 4. Graph Historical Data

Use ApexCharts or similar to visualize the 13-month history:

```yaml
type: custom:apexcharts-card
header:
  show: true
  title: Water Consumption - 13 Month History
graph_span: 13mon
series:
  - entity: sensor.water_meter_value
    name: Monthly Usage
    type: column
    data_generator: |
      const monthly = entity.attributes.monthly_usage.slice(1); // Skip first (unknown)
      const now = new Date();
      return monthly.map((usage, i) => {
        const date = new Date(now.getFullYear(), now.getMonth() - (12 - i), 1);
        return [date.getTime(), usage];
      });
```

## Code Implementation

### Structure Update (`src/cc1101.h`)
```cpp
struct tmeter_data {
  int liters;
  int reads_counter;
  int battery_left;
  int time_start;
  int time_end;
  int rssi;
  int rssi_dbm;
  int lqi;
  int8_t freqest;
  uint32_t history[13];        // NEW: Monthly historical readings
  bool history_available;       // NEW: True if historical data extracted
};
```

### Data Extraction (`src/cc1101.cpp`)
The `parse_meter_report()` function extracts historical data from bytes [66-117] of the decoded meter payload:

```cpp
// Extract 13 months of historical volume data if buffer is large enough
if (size >= 118) {
  data.history_available = true;
  for (int i = 0; i < 13; i++) {
    int offset = 66 + (i * 4);
    data.history[i] = ((uint32_t)decoded_buffer[offset]) |
                      ((uint32_t)decoded_buffer[offset + 1] << 8) |
                      ((uint32_t)decoded_buffer[offset + 2] << 16) |
                      ((uint32_t)decoded_buffer[offset + 3] << 24);
  }
}
```

### MQTT Publishing (`src/main.cpp`)
After publishing the main liters value, the code constructs and publishes a JSON attributes message containing:
- `history`: Array of 13 historical volume readings
- `monthly_usage`: Calculated consumption for each month (difference between consecutive readings)
- `current_month_usage`: Usage in the current partial month
- `months_available`: Always 13

## Limitations and Notes

### No Exact Timestamps
The basic RADIAN protocol **does not provide exact dates** for the historical readings. The meter's internal RTC tracks when these snapshots were taken, but this information is not transmitted in the basic query response. 

**Assumption**: The values represent end-of-month snapshots. This is based on:
1. The meter having 13 values (13 months of history)
2. The most recent value matching the volume at the start of the current month
3. Standard utility billing practices (monthly cycles)

### Month Boundary Timing
The exact timing of when the meter captures each monthly snapshot is unknown. It could be:
- Calendar month end (last day at midnight)
- Billing cycle date (e.g., 15th of each month)
- Rolling 30-day intervals

Based on user observation: *"713,744 was around the very start of the current month/reading at the end of the previous month"* - this suggests end-of-month or billing cycle snapshots.

### First Month Usage
The `monthly_usage[0]` is set to `0` because we don't have a baseline reading from before the oldest historical value. To calculate actual usage for that month, you would need 14 months of data.

### Buffer Size Requirements
Historical data extraction requires the decoded meter payload to be at least 118 bytes. If a shorter payload is received, `history_available` will be `false` and no attributes will be published.

## Enhanced Meter Capabilities

The Itron EverBlu Cyble Enhanced meter actually stores much more data internally:
- **181 consumption intervals** (hourly/daily/weekly/monthly)
- **Peak flow rates with timestamps**
- **Alarm logs with start/end times**
- **Time-of-Use indexes**
- **Backflow/leakage history**

However, these enhanced features require **proprietary Itron commands** that are not part of the open-source RADIAN protocol implementation. The basic RADIAN protocol only exposes:
- Current total volume
- Battery remaining (months)
- Read counter
- Wake schedule (hours)
- **13 monthly historical volumes** (no dates)

## Troubleshooting

### Attributes Not Appearing
1. Verify the meter is transmitting a full payload:
   - Enable debug mode (`debug_out = 1` in `cc1101.cpp`)
   - Check serial output for: `> Decoded meter data size = 122`
   - If size < 118, historical data won't be extracted

2. Check MQTT messages:
   - Subscribe to `everblu/cyble/liters_attributes` in MQTT Explorer
   - Verify JSON is being published after each meter read

3. Restart Home Assistant:
   - After first publish, restart HA to ensure it recognizes the new attributes
   - Clear browser cache if Developer Tools doesn't show attributes

### Incorrect Monthly Values
If the historical values don't make sense:
1. Verify `history[12]` matches your reading from ~30 days ago
2. Check that `current_month_usage` = `current_liters - history[12]`
3. Ensure monthly_usage calculations are sequential increases (no decreases unless meter was replaced/reset)

### JSON Buffer Overflow
The `historyJson` buffer is sized at 800 bytes. For very large meter readings (>999,999,999 liters), the JSON might be truncated. Monitor serial output for warnings.

## Future Enhancements

Potential improvements for future versions:

1. **Date Estimation**: Use the system date when each reading occurs to estimate historical month labels
2. **Persistent Storage**: Store historical readings in EEPROM/SPIFFS to detect when months roll over
3. **Change Detection**: Detect when `history[12]` changes (new month boundary) and log/publish an event
4. **Statistics Integration**: Integrate with Home Assistant's long-term statistics for better historical graphing
5. **Enhanced Protocol**: Reverse engineer the proprietary Itron protocol to get actual timestamps and all 181 intervals

## References

- **RADIAN Protocol**: Itron proprietary, basis for Wireless M-Bus EN 13757-4 Mode F
- **Meter Model**: Itron EverBlu Cyble Enhanced (e.g., 2020-0257750)
- **Payload Bytes**: [66-117] contain 13 × uint32_t historical volumes (LSB first)
- **Home Assistant MQTT Discovery**: [MQTT Sensor Documentation](https://www.home-assistant.io/integrations/sensor.mqtt/)
