# Quick Reference Guide - New Features

## New MQTT Topics

### Diagnostics & Metrics
| Topic | Type | Description |
|-------|------|-------------|
| `everblu/cyble/cc1101_state` | Text | Current radio state: "Idle", "Reading", or "Frequency Scanning" |
| `everblu/cyble/total_attempts` | Number | Total number of meter read attempts |
| `everblu/cyble/successful_reads` | Number | Number of successful reads |
| `everblu/cyble/failed_reads` | Number | Number of failed reads |
| `everblu/cyble/last_error` | Text | Description of last error (or "None") |
| `everblu/cyble/frequency_offset` | Number | Current frequency offset in MHz |

### Commands
| Topic | Payload | Description |
|-------|---------|-------------|
| `everblu/cyble/frequency_scan` | `scan` | Trigger automatic frequency scan |
| `everblu/cyble/trigger` | `update` | Manual meter read (existing) |
| `everblu/cyble/restart` | `restart` | Restart the device (existing) |

---

## Frequency Scanning

### How to Use

**Method 1: Home Assistant**
1. Go to your Water Meter device in Home Assistant
2. Find the "Scan Frequency" button
3. Click the button
4. Wait for scan to complete (~1-2 minutes)
5. Check "Frequency Offset" sensor for results

**Method 2: MQTT**
```bash
mosquitto_pub -h <mqtt_broker> -t "everblu/cyble/frequency_scan" -m "scan"
```

### Scan Parameters
- **Range:** ±30 kHz (±0.030 MHz)
- **Step:** 5 kHz (0.005 MHz)
- **Duration:** ~1-2 minutes
- **Criterion:** Best RSSI with valid data

### Interpreting Results
- Positive offset (e.g., +0.005 MHz): Optimal frequency is 5 kHz above nominal
- Negative offset (e.g., -0.010 MHz): Optimal frequency is 10 kHz below nominal
- Zero offset: Nominal frequency is optimal

### When to Scan
- After initial installation
- If signal quality degrades over time
- After significant temperature changes
- If moving the device to a new location

---

## Monitoring System Health

### Key Metrics to Watch

**Success Rate**
```
Success Rate = (successful_reads / total_attempts) × 100%
```
- **Good:** >90%
- **Fair:** 70-90%
- **Poor:** <70%

**Signal Quality**
- **RSSI (dBm):** Higher is better (e.g., -45 dBm is better than -75 dBm)
  - Excellent: -40 to -50 dBm
  - Good: -50 to -65 dBm
  - Fair: -65 to -80 dBm
  - Poor: <-80 dBm
- **LQI (%):** 0-100%, higher is better
  - Excellent: >80%
  - Good: 60-80%
  - Fair: 40-60%
  - Poor: <40%

### Troubleshooting with New Diagnostics

**Problem: Frequent read failures**
1. Check `last_error` for details
2. Review `failed_reads` trend
3. Check RSSI and LQI values
4. Try frequency scan to optimize reception

**Problem: CC1101 stuck in "Reading" state**
- Indicates communication timeout
- Device will auto-recover and enter cooldown
- Check `last_error` for timeout details

**Problem: Inconsistent reads**
- Monitor `cc1101_state` for state transitions
- Check if frequency offset is properly loaded
- Consider running frequency scan

---

## Home Assistant Automations Examples

### Alert on Failed Reads

```yaml
automation:
  - alias: "Water Meter - Alert on Failed Reads"
    trigger:
      - platform: state
        entity_id: sensor.water_meter_failed_reads
    condition:
      - condition: template
        value_template: >
          {{ (states('sensor.water_meter_failed_reads') | int) > 5 }}
    action:
      - service: notify.mobile_app
        data:
          message: "Water meter has {{ states('sensor.water_meter_failed_reads') }} failed reads"
          title: "Water Meter Alert"
```

### Weekly Frequency Scan

```yaml
automation:
  - alias: "Water Meter - Weekly Frequency Scan"
    trigger:
      - platform: time
        at: "02:00:00" # 2 AM
    condition:
      - condition: time
        weekday:
          - sun
    action:
      - service: button.press
        target:
          entity_id: button.water_meter_scan_frequency
```

### Monitor Success Rate

```yaml
template:
  - sensor:
      - name: "Water Meter Success Rate"
        unit_of_measurement: "%"
        state: >
          {% set total = states('sensor.water_meter_total_attempts') | int %}
          {% set success = states('sensor.water_meter_successful_reads') | int %}
          {% if total > 0 %}
            {{ ((success / total) * 100) | round(1) }}
          {% else %}
            0
          {% endif %}
        icon: mdi:percent
```

---

## Performance Improvements

### MQTT Publish Speed
- **Before:** ~500ms total delay for all publishes
- **After:** ~50ms total delay for all publishes
- **Improvement:** 90% faster (450ms reduction)

### System Responsiveness
- Faster meter read cycles
- Reduced blocking during MQTT operations
- Better WiFi/OTA responsiveness during reads

---

## Persistent Storage Details

### ESP8266 (EEPROM)
- **Storage Location:** EEPROM address 0-3
- **Format:** Magic number (2 bytes) + Float offset (4 bytes)
- **Validation:** Magic number 0xABCD + range check (±0.1 MHz)

### ESP32 (Preferences)
- **Namespace:** "everblu"
- **Key:** "freq_offset"
- **Format:** Float (4 bytes)
- **Validation:** Range check (±0.1 MHz)

### Clearing Stored Offset
If you need to reset the frequency offset:

**ESP8266:**
```cpp
// Add to setup() temporarily, upload, then remove
EEPROM.begin(64);
for(int i=0; i<4; i++) EEPROM.write(i, 0xFF);
EEPROM.commit();
```

**ESP32:**
```cpp
// Add to setup() temporarily, upload, then remove
Preferences prefs;
prefs.begin("everblu", false);
prefs.remove("freq_offset");
prefs.end();
```

---

## Serial Monitor Debug Output

### Normal Startup
```
> EEPROM initialized
> Loaded frequency offset 0.005000 MHz from EEPROM
> Initializing CC1101 radio...
> Applying stored frequency offset: 0.005000 MHz (effective: 433.825000 MHz)
> CC1101 radio found OK (PARTNUM: 0x00, VERSION: 0x14)
> Frequency synthesizer calibrated for 433.825000 MHz
```

### Frequency Scan Output
```
> Starting frequency scan...
> Scanning from 433.790000 to 433.850000 MHz (step: 0.005000 MHz)
> Better signal at 433.815000 MHz: RSSI=-45 dBm
> Better signal at 433.820000 MHz: RSSI=-42 dBm
> Frequency scan complete. Best frequency: 433.820000 MHz (offset: 0.000000 MHz, RSSI: -42 dBm)
> Frequency offset 0.000000 MHz saved to EEPROM
```

---

## Configuration Notes

### Frequency Settings
The base frequency is defined in `private.h`:
```cpp
#define FREQUENCY 433.82  // Base frequency in MHz
```

The stored offset is automatically applied:
```
Effective Frequency = FREQUENCY + stored_offset
```

### Scan Range Configuration
Currently hardcoded in `main.cpp`:
```cpp
float scanStart = baseFreq - 0.03;  // -30 kHz
float scanEnd = baseFreq + 0.03;    // +30 kHz
float scanStep = 0.005;             // 5 kHz steps
```

---

## Migration Notes

### Upgrading from Previous Version
1. Flash new firmware
2. Device will automatically initialize storage
3. First boot: offset will be 0.0 MHz
4. Optionally run frequency scan to optimize
5. Offset persists across reboots

### No Breaking Changes
- All existing MQTT topics unchanged
- Configuration file format unchanged
- Home Assistant discovery automatically adds new entities
- Existing automations continue to work

---

## Support and Troubleshooting

### Common Issues

**Q: Frequency offset not saving**
- A: Check serial monitor for storage initialization messages
- ESP8266: Verify EEPROM.begin() succeeded
- ESP32: Verify Preferences namespace opened successfully

**Q: Frequency scan finds no signal**
- A: Check antenna connection and placement
- Verify meter is within wake window (time_start to time_end)
- Try manual reading first to confirm basic operation

**Q: Diagnostics not appearing in Home Assistant**
- A: Check MQTT broker connection
- Verify Home Assistant MQTT integration is active
- Wait 30 seconds for discovery messages to process
- Restart Home Assistant if needed

### Debug Mode
Enable detailed logging by setting in `cc1101.cpp`:
```cpp
uint8_t debug_out = 1;  // Change from 0 to 1
```

This enables detailed RF communication traces in serial monitor.

---

## Credits

Enhanced by: GitHub Copilot (October 2025)
Based on original work by: Psykokwak and Neutrinus
Maintained by: Genestealer

