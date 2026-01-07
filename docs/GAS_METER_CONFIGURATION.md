# Gas Meter Configuration Guide

This document provides comprehensive information about configuring and using this firmware with EverBlu Cyble **gas meters**.

## Table of Contents

- [Overview](#overview)
- [Configuration](#configuration)
- [Understanding the Volume Divisor](#understanding-the-volume-divisor)
- [Real-World Example](#real-world-example)
- [Troubleshooting](#troubleshooting)

---

## Overview

The EverBlu Cyble Enhanced RF module is used on both water and gas meters. This firmware supports both types seamlessly through the `METER_TYPE` configuration option.

**Key Differences:**
- **Water Meters:** Readings in liters (L)
- **Gas Meters:** Readings in cubic meters (m³)

The RADIAN protocol transmits consumption data as a 4-byte integer representing the internal meter reading in **liters**. For gas meters, this firmware automatically converts to cubic meters (m³) using a configurable divisor.

---

## Configuration

### Step 1: Set Meter Type

In `include/private.h`, set:
```cpp
#define METER_TYPE "gas"
```

This configures Home Assistant device class, icons, and unit symbols appropriately for a gas meter.

### Step 2: Configure Volume Divisor

In `include/private.h`, set the `GAS_VOLUME_DIVISOR`:
```cpp
// Default: 100 (0.01 m³ per unit)
#define GAS_VOLUME_DIVISOR 100
```

The divisor value depends on your meter's pulse weight configuration:
- **100:** 0.01 m³ per unit (typical for modern gas meters)
- **1000:** 0.001 m³ per unit (legacy or alternative configuration)

---

## Understanding the Volume Divisor

### How It Works

The RADIAN protocol delivers a raw counter value in liters. The firmware converts this to cubic meters using:

```
volume_m³ = raw_counter_liters / GAS_VOLUME_DIVISOR
```

### Pulse Weight vs. Divisor

**Pulse Weight** is the amount of gas measured per one unit increment in the meter's counter.

| Pulse Weight | Unit | Divisor | Example |
|---|---|---|---|
| 0.01 m³ | per unit | 100 | counter=81415 → 814.15 m³ |
| 0.001 m³ | per unit | 1000 | counter=81415 → 81.415 m³ |
| 1 L (0.001 m³) | per unit | 1000 | counter=81415 → 81.415 m³ |

### Where to Find Your Meter's Pulse Weight

1. **Check the meter label** – Look for "Pulse weight" or "Gewicht pro Impuls"
2. **Consult the manufacturer documentation** – EverBlu brochures often list this
3. **Contact your utility provider** – They can confirm the configuration
4. **Empirical verification** – Compare RADIAN readings to the physical meter display

---

## Real-World Example

This discovery of the correct divisor was determined through empirical testing with an actual EverBlu Cyble gas meter.

### Observed Data

| Metric | Value |
|--------|-------|
| Physical meter register | 825,292 m³ |
| RADIAN raw counter | 0x00013E07 = 81,415 units |
| Date of observation | January 6, 2026 |

### Testing Different Divisors

**Using divisor 1000 (incorrect):**
- Calculation: 81,415 / 1000 = 81.415 m³
- Comparison to register: Off by ~744 m³ (implausible)
- Conclusion: Wrong configuration

**Using divisor 100 (correct):**
- Calculation: 81,415 / 100 = 814.15 m³
- Comparison to register: Off by ~11 m³ (reasonable)
- Conclusion: Correct configuration

### Discovery Notes

Through empirical testing with actual EverBlu Cyble gas meters, we discovered that many gas modules are configured with a pulse weight of **0.01 m³ per unit**, not the 0.001 m³ that might be expected from a naive "liters to m³" conversion.

In the observed case presented above:
- Physical meter register: 825,292 m³
- RADIAN data (raw): 0x00013E07 = 81,415 units
- Using divisor 1000: 81.415 m³ (incorrect, off by ~744 m³)
- Using divisor 100: 814.15 m³ (reasonable, ~11 m³ gap from register)

The 0.01 m³/unit configuration proved more plausible through trial and error comparison with the actual mechanical meter register.

### Why the 11 m³ Gap?

The ~11 m³ gap between the RADIAN reading (814.15 m³) and the physical register (825.292 m³) is consistent with the assumption that **the EverBlu module was installed after the meter had already accumulated significant consumption**.

**Example timeline:**
- Day 1: Meter installed on customer premises (initial reading: 0 m³)
- Day N: Meter accumulates 825,292 m³ of consumption
- Day N+X: EverBlu module retrofitted (starts recording from 814.15 m³ baseline)

Without access to the meter's installation records or multiple data points, we cannot definitively confirm the exact pulse weight; however, this scenario is consistent with the observed data.

This means the firmware is reading absolute consumption correctly; the difference represents consumption that occurred before the EverBlu was installed.

---

## Troubleshooting

### Symptom: Readings are exactly 10x too high or too low

**Cause:** Incorrect divisor configuration

**Solution:**
1. Verify your meter's pulse weight from the label or manufacturer documentation
2. Update `GAS_VOLUME_DIVISOR` accordingly:
   - If readings are 10x too high: increase divisor (1000 → ?)
   - If readings are 10x too low: decrease divisor (100 → ?)
3. Rebuild and upload the firmware
4. Verify against the physical meter display

### Symptom: Readings seem approximately correct but don't match the meter register exactly

**Possible causes:**

1. **EverBlu module was installed after initial consumption**
   - Compare the RADIAN value trend to meter register changes
   - Difference should remain constant if module is working correctly

2. **Unit mismatch in verification**
   - Ensure you're comparing m³ to m³ (not L to m³)
   - Check meter label for unit symbols

3. **Historical data available for offset calibration**
   - The firmware publishes monthly historical readings
   - Use these to establish a baseline and verify consistency over time

### Symptom: Cannot determine correct divisor from meter label

**Procedure:**

1. **Record initial RADIAN reading** (e.g., counter = 81,415 units)
2. **Document physical meter display** (e.g., 825,292 m³)
3. **Wait 7-30 days for measurable consumption**
4. **Record both values again** and calculate the delta
5. **Compare:**
   - RADIAN delta (units) to meter register delta (m³)
   - Ratio should match pulse weight:
     - If meter advanced 100 m³ and RADIAN advanced 10,000 units → divisor = 100
     - If meter advanced 100 m³ and RADIAN advanced 100,000 units → divisor = 1000

---

## Advanced: Adapting for Non-Standard Configurations

If your meter uses an uncommon pulse weight, you can calculate the correct divisor:

```
GAS_VOLUME_DIVISOR = (RADIAN_counter_units / actual_volume_m³)
```

**Example:** If your meter uses 0.1 m³ per unit:
- RADIAN counter: 81,415 units
- Actual volume: 8,141.5 m³
- Divisor: 81,415 / 8,141.5 ≈ 10

Then set: `#define GAS_VOLUME_DIVISOR 10`

---

## Summary

| Task | Action |
|------|--------|
| Initial setup | Set `METER_TYPE "gas"` and `GAS_VOLUME_DIVISOR 100` |
| Verify readings | Compare first RADIAN reading to meter register |
| If readings are wrong | Check meter label for pulse weight, adjust divisor |
| Monitor over time | Verify monthly consumption trends match physical meter |

