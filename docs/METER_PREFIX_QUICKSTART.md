# Quick Start: Meter Prefix Configuration

## TL;DR

Want to keep your Home Assistant history when running a **single meter**?

Add this line to your `private.h` file:

```cpp
#define ENABLE_METER_PREFIX_IN_ENTITY_IDS 0
```

Then recompile and upload. Done! Your entities will keep their original names without the meter serial prefix.

---

## Default Behavior

- **Default value:** `1` (enabled - includes meter serial prefix)
- **No action required** if you're running multiple meters
- **Backward compatible** - existing configurations continue to work

---

## When to Change This

| Scenario | Setting | Reason |
|----------|---------|--------|
| Running a **single meter** for over a year | `0` | Preserve Home Assistant history |
| Running **multiple meters** on same broker | `1` | Avoid MQTT topic collisions and data corruption |
| First time setup | `1` | Recommended default |
| Upgrading from v1.15 | `0` | Restore old entity naming |

---

## Entity ID Examples

### With Prefix Enabled (Default)
```
everblu_meter (meter 257750) → 257750_everblu_meter_value
everblu_meter (meter 2777550) → 2777550_everblu_meter_value
```

### With Prefix Disabled
```
All meters → everblu_meter_value
```

---

## Where to Find It

File: `include/private.h`

Look for this section:
```cpp
// Debugging
// 1 = enable verbose MQTT output on serial
// 0 = disable (default)
#define ENABLE_MQTT_DEBUGGING 0

// Meter number prefix in entity IDs
//
// 1 (default): Include meter serial as prefix
// 0:           Omit meter serial prefix
//
#define ENABLE_METER_PREFIX_IN_ENTITY_IDS 1  ← CHANGE THIS
```

---

## After Changing

1. Save the file
2. Recompile: `pio run`
3. Upload to device
4. Restart Home Assistant (recommended) or refresh MQTT discovery

---

## Need Help?

See the full guide in [`docs/METER_PREFIX_CONFIGURATION.md`](METER_PREFIX_CONFIGURATION.md)
