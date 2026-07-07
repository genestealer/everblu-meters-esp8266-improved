# Frequency Calibration System — Design Reference

*Implemented 2026-07-03. Covers the CC1101 RX bandwidth widening, two-phase scan algorithm, FREQEST adaptive tracking, and Fast-scan removal.*

---

## 1. The problem that was being solved

The EverBlu Cyble Enhanced broadcasts at a nominal **433.82 MHz** carrier (RADIAN protocol, 2-FSK, ±5.157 kHz deviation, 2.4 kbps). The firmware reads the meter by transmitting a 2-second wake-up burst and then listening for an ACK + data frame.

### Why the radio couldn't lock at 433.82 MHz

The CC1101 radio module on the ESP8266 contains a cheap **26 MHz ceramic resonator** as its frequency reference. This crystal is often significantly off-spec. A ~154 ppm error (67 kHz at 433 MHz) was observed on the development module — nearly 4× the CC1101's own ±40 ppm crystal specification.

Before this work the CC1101 was configured with:

| Register | Value | Meaning |
|---|---|---|
| `MDMCFG4` | `0xF6` | RX channel filter bandwidth = **58 kHz** |
| `FOCCFG` | `0x1D` | Frequency-offset compensation (FOC) limit = **±BW/8 = ±7.25 kHz** |

A 67 kHz crystal error means the meter's signal fell **entirely outside the 58 kHz receive filter** unless the local oscillator was manually walked onto it. And even if the signal happened to be near the band edge, the chip's own FOC could only self-correct ±7.25 kHz.

This is why the firmware had accumulated a large amount of software frequency-scanning machinery just to hear the meter at all.

---

## 2. Root-cause distinction

> **The meter is not the problem.** The meter broadcasts at exactly its design frequency. The reference crystal in the CC1101 radio *module* connected to the ESP is the source of the error, and it causes every frequency the CC1101 tunes to be shifted by a proportional amount.

$$\Delta f_{error} = f_{nominal} \times \frac{\text{crystal error (ppm)}}{10^6}$$

At 154 ppm and 433 MHz: $433 \times 0.000154 \approx 67 \text{ kHz}$.

---

## 3. Fix: widen the RX filter and raise FOC_LIMIT

### Register changes in `src/core/cc1101.cpp`

| Register | Before | After | Effect |
|---|---|---|---|
| `MDMCFG4` | `0xF6` (58 kHz BW) | `0x66` (270 kHz BW) | Filter catches the signal despite crystal error |
| `FOCCFG` | `0x1D` (±BW/8 = ±7.25 kHz) | `0x1E` (±BW/4 = ±67.7 kHz) | Chip self-corrects up to ±156 ppm automatically |

### Why 270 kHz?

BW formula: $\text{BW} = \frac{F_{xosc}}{8 \cdot (4 + \text{CHANBW\_M}) \cdot 2^{\text{CHANBW\_E}}}$

`0x66` → CHANBW_E=1, CHANBW_M=2, DRATE_E=6 (data rate **unchanged**):

$$\text{BW} = \frac{26{,}000}{8 \times 6 \times 2} = \mathbf{270.8 \text{ kHz}}$$

### Why ±BW/4 for FOC?

$$\text{FOC\_LIMIT} = \pm\frac{270.8}{4} = \pm 67.7 \text{ kHz} \approx \pm 156 \text{ ppm at 433 MHz}$$

This just covers the development module's 154 ppm error. Any in-spec CC1101 (≤40 ppm → ≤17 kHz) is covered with huge margin.

### Noise floor cost

The RADIAN signal occupies only ~15 kHz (Carson's rule: 2 × (deviation + bit_rate/2) = 2 × (5.157 + 1.2) ≈ 12.7 kHz). Widening the filter from 58 kHz to 270 kHz adds:

$$\Delta \text{noise} = 10 \log_{10}\left(\frac{270}{58}\right) \approx +6.7 \text{ dB}$$

The observed link margin at normal installation distance is ~27 dB (signal −82 dBm vs. noise floor −109 dBm), so the 6.7 dB cost is well within budget.

### Practical result

With the 270 kHz filter and ±67.7 kHz FOC, the CC1101 **locks onto the meter at the nominal 433.82 MHz with no software scan**. Frequency scanning is now a fallback for extreme drift only.

---

## 4. Frequency calibration system (after this work)

There are three layers, applied in sequence:

```
Layer 1 — EEPROM stored offset (coarse, survives reboots)
  ↓  applied at boot via cc1101_init(baseFreq + storedOffset)

Layer 2 — CC1101 FOC (±67.7 kHz hardware, corrects within ~1 receive frame)
  ↓  automatic, no firmware involvement

Layer 3 — FREQEST adaptive tracking (fine, ~1.59 kHz/LSB, post-receive)
  ↓  firmware reads FREQEST after each successful decode
     accumulates over ADAPTIVE_THRESHOLD reads
     applies 50% of measured error to storedOffset
     saves back to EEPROM
```

### Where the offset lives

The CC1101 has **no non-volatile memory**. Its FREQ registers are volatile and reprogrammed from scratch on every `cc1101_init()` call. The offset is stored by the ESP8266 firmware in **flash (emulated EEPROM)** via the `StorageAbstraction` layer. It persists across power cuts, reboots, and OTA updates.

### FREQEST resolution

$$\Delta f_\text{LSB} = \frac{F_{xosc}}{2^{14}} = \frac{26{,}000{,}000}{16{,}384} \approx 1{,}587 \text{ Hz} \approx 1.59 \text{ kHz/LSB}$$

After one successful read, the adaptive correction can resolve the residual error to within ~0.8 kHz (50% of 1 LSB).

---

## 5. Deep frequency scan (fallback)

Used when the CC1101's FOC cannot bridge the gap (crystal error > ±67.7 kHz, very weak signal, or first boot with no stored offset). The scan is a two-phase algorithm.

### Entry points

| Trigger | Range | Step | Duration |
|---|---|---|---|
| MQTT `deep_scan` command or startup | ±150 kHz | 2.5 kHz | ≤ 6 min |
| Auto-scan after `MAX_RETRIES` failures | ±20 kHz | 1 kHz | ≤ 2 min |

Auto-scan-on-failure uses the narrow call `performDeepFrequencyScan(0.020f, 0.001f)` because by the time MAX_RETRIES is reached the firmware is already close to the carrier.

### Phase 1 — Window mapping (coarse pass)

```
for each freq step from baseFreq - range to baseFreq + range:
    try to decode meter

    if reads_counter > 0:
        record firstHitFreq (first time)
        update lastHitFreq
        reset consecutiveMisses = 0

    else if we have seen at least one hit:
        consecutiveMisses++
        if consecutiveMisses >= MISS_TOLERANCE (5):
            break  ← window edge found
```

This maps the full response band (`firstHitFreq` to `lastHitFreq`) without scanning to the end of the range unnecessarily.

**Note on `reads_counter`:** this value is the meter's **lifetime internal read counter** extracted from the decoded payload — the same for every successful step. It is binary (zero = CRC fail, non-zero = CRC pass) and cannot be used to rank scan steps by quality.

**Note on consecutive misses:** the meter's 2-second broadcast cycle and the 3-second scan step window mean approximately 50% of on-frequency steps miss by timing. `MISS_TOLERANCE = 5` tolerates this without prematurely declaring the window closed.

### Phase 2 — Zoom (fine pass)

```
zoomStart = firstHitFreq - scanStep
zoomEnd   = lastHitFreq  + scanStep
zoomStep  = max(scanStep × 0.25,  CC1101_MIN_STEP_MHZ)  ← hardware minimum clamped

for each zfreq from zoomStart to zoomEnd:
    try to decode meter
    if reads_counter > 0:
        bestFreq = zfreq
        break  ← first CRC-valid hit
```

Falls back to `windowMidFreq` if all zoom steps miss (timing variance). The FREQEST adaptive tracking then refines from wherever the scan landed.

**Why stop at first zoom hit:** stopping at first CRC-valid hit is correct because (a) all successful steps return the same `reads_counter` value — there is no quality ranking possible; (b) RSSI varies by 1–3 dB across adjacent steps due to measurement noise and is not a reliable centre indicator; (c) the FREQEST loop provides the actual fine-tuning.

### CC1101 minimum frequency step (hardware limit)

$$\Delta f_\text{min} = \frac{F_{xosc}}{2^{16}} = \frac{26{,}000{,}000}{65{,}536} \approx 396.7 \text{ Hz}$$

Requesting a zoom step finer than this rounds consecutive steps to the same register value, retesting the identical physical frequency. The zoom step is clamped to at least `CC1101_MIN_STEP_MHZ = 26/65536/1000 ≈ 0.000397 MHz`.

For the 1 kHz auto-scan: `1000 Hz × 0.25 = 250 Hz` → clamped to **397 Hz**.
For the 2.5 kHz manual deep scan: `2500 Hz × 0.25 = 625 Hz` → already above limit, unchanged.

### Save and reinit

```
offset = bestFreq - baseFrequency
saveFrequencyOffset(offset)     ← writes to ESP flash (EEPROM)
cc1101_init(baseFreq + offset)  ← reprograms CC1101 FREQ registers
```

FREQEST adaptive tracking then refines and re-saves after subsequent successful reads.

---

## 6. Fast scan removal

The **Fast frequency scan** (±150 kHz, 10 kHz steps) was removed entirely. It was redundant because:

1. The two-phase Deep scan's Phase 1 coarse pass already does the equivalent acquisition in fewer wasted steps (exits at the window edge, not the end of the range).
2. The 10 kHz steps give at best ±5 kHz accuracy before FREQEST; the Deep scan gives ±0.4 kHz.
3. Most modules now lock without any scan due to the 270 kHz bandwidth change.

**Removed from both builds:**
- `FrequencyManager::performFastFrequencyScan()` (shared service)
- `MeterReader::performFrequencyScan(bool deep)` → simplified to `performFrequencyScan()` (always deep)
- `performFastFrequencyScan()` in `main.cpp` (MQTT standalone)
- MQTT `fast_scan` command + Home Assistant "Fast Scan" button
- ESPHome `fast_scan_button` config option, `EverbluMeterTriggerButton::is_fast_scan_` flag, `request_fast_scan()`
- `fast_scan_button` from all YAML examples, CI config, and README

**Replacement:** use `deep_scan_button` / MQTT `deep_scan` command instead.

---

## 7. AGC — known limitation (separate issue)

`AGCCTRL2 = 0xC7` locks the receiver near maximum gain. At close range this saturates the front-end: RSSI reads a flat −31 dBm plateau across ~40 kHz, frames are received but every CRC fails — classic overload, not a frequency problem.

Tracked as [GitHub issue #109](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/109). Independent of the bandwidth work; to be addressed separately.

---

## 8. Affected files

| File | Change |
|---|---|
| `src/core/cc1101.cpp` | `MDMCFG4 0xF6→0x66` (270 kHz BW), `FOCCFG 0x1D→0x1E` (±BW/4 FOC). Shared by both builds. |
| `src/services/frequency_manager.cpp/.h` | Removed `performFastFrequencyScan()`. Added `scanRangeMHz`/`scanStepMHz` params to `performDeepFrequencyScan()`. Two-phase scan algorithm. Zoom step clamped to hardware minimum. |
| `src/services/meter_reader.cpp/.h` | `performFrequencyScan(bool)` → `performFrequencyScan()`. Auto-scan-on-failure uses narrow `(0.020f, 0.001f)` call. |
| `src/main.cpp` | Removed `performFastFrequencyScan()`, `fast_scan` MQTT handler, HA "Fast Scan" button discovery. `performDeepFrequencyScan()` parameterised. |
| `ESPHOME/components/everblu_meter/everblu_meter.cpp/.h` | Removed `request_fast_scan()`, `is_fast_scan_` flag. |
| `ESPHOME/components/everblu_meter/__init__.py` | Removed `CONF_FAST_SCAN_BUTTON`. Updated all button codegen blocks. |
| `ESPHOME/example-*.yaml` | Removed `fast_scan_button` entries. |
| `.ci/esphome/everblu_meter/common-full.yaml` | Removed `fast_scan_button` CI entry. |
| `include/private.example.h` | Updated `FREQUENCY` comment: new default-is-sufficient guidance. |
| `ESPHOME/README.md` | Removed `fast_scan_button` entry from control-buttons table. |

---

## 9. Commits (develop branch)

| Hash | Description |
|---|---|
| `f5d905b` | feat: two-phase deep frequency scan with window mapping and zoom |
| `13d0fbd` | fix: clamp zoom step to CC1101 minimum frequency resolution (~397 Hz) |
| `0ae5c4c` | feat: widen CC1101 RX bandwidth to 270 kHz + remove Fast scan |
