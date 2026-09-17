# Full Code Review: everblu-meters ESP8266/ESP32 (ESPHome + MQTT)

**Date:** 2026-09-09
**Branch reviewed:** `develop`
**Scope:** Whole project (correctness, security, cleanup/simplification, and Home Assistant / ESPHome idiom).

This document is self-contained so it can be imported into another chat as context.

## Post-review updates

**2026-09-10, commit `438335c` "feat(reader): read once more after a scan stores new tuning"** (reviewed)

Touches `src/services/meter_reader.cpp` (+ the generated `ESPHOME-release/` copy) and adds tests. Assessment:

- **Clean, no new findings.** Adds a one-shot post-scan confirmation read: `finishFrequencyScan()` queues a read when the scan outcome is `Found` *and* the offset changed; `loop()` takes it; a miss is treated as final (no fresh retry, no immediate re-scan). All three scan entry points (`performFrequencyScan()` for boot auto-scan / manual / button, and the auto-scan-on-failure path) set `m_offsetBeforeScan`, so the `offset != m_offsetBeforeScan` check has no stale-value path.
- **Fixes a latent bug not previously flagged:** now clears `m_inCooldown` on a successful read. Without it, a success during cooldown left scheduled reads blocked for the rest of the hour.
- **Adds genuine test coverage** (3 new tests: confirmation-read-on-new-offset, no-read-when-unchanged, failed-confirmation-ends-streak), real behavioural assertions, not tautologies.
- **Does NOT resolve any existing HIGH/MEDIUM finding.** The scheduled-skip (#2), DST (#7), hour/minute clamp (#10), and retry-timer rollover findings live in untouched code.
- **Line numbers shifted** in `meter_reader.cpp`; the references below have been updated (e.g. #2 `315→329`, retry-timer `236→249`). Findings #7/#10 remain at ~lines 118–127.

**2026-09-10, fixes applied on branch `fix/code-review-2026-09`** (working through the prioritized fix list top to bottom, one commit per item):

- ✅ **History `averageMonthlyUsage`**: removed the undocumented, unused field and its test assertions (`9ef0ad8`).
- ✅ **#2 scheduled-read skip**: the read now fires anywhere inside the scheduled minute, guarded once-per-day by a year-aware date key (`ScheduleManager::dateKey()`); added a mid-minute host test (`28212d3`).
- ✅ **P1 #3 advanced example**: added the missing `id: meter_status` (`8138ed1`).
- ✅ **`prepare-component-release.sh` `\s`**: switched to POSIX `[[:space:]]+` for BSD/macOS sed portability (`1515f42`).
- ✅ **Doc corrections**: `auto_scan` default `true→false` in the integration guide; the three superseded frequency docs (`ADAPTIVE_FREQUENCY_FEATURES.md`, `QUICK_REFERENCE.md`, `IMPROVEMENTS_SUMMARY.md`) now carry a "historical" banner; the timestamp `device_class` finding was confirmed a non-defect (see below).
- ✅ **`release.yml` injection hardening**: moved `github.event.inputs.tag`/`event_name` into `env:` and referenced them quoted, out of the inline `run:` script (`c176280`).
- ✅ **P1 #8 button-guard consistency**: `request_manual_read()`/`request_deep_scan()` now honour the same `meter_initialized_`/in-progress guards as the other command entry points (`1fa3bda`).
- ✅ **P1 #9 RSSI dBm type widening**: `cc1100_rssi_convert2dbm` return type and the diagnostic struct field widened `int8_t→int` so a valid negative dBm can't underflow; added datasheet + full-sweep host tests (`fb57c02`).
- ✅ **CodeQL C/C++ coverage**: documented the intentional exclusion (native host suites + cppcheck cover C/C++) in `codeql.yml` (`0e28374`).
- ✅ **P1 #10 MeterReader/ScheduleManager consolidation**: extracted stateless `matchesReadingDay`/`localReadingTime` helpers as the single source of truth (both classes delegate), which also clamps out-of-range configured read times; added a clamp host test (`0597fa2`).
- ✅ **MQTT NTP de-blocking**: the connect callback now kicks off `configTzTime()` and returns immediately; a non-blocking `pollNtpSync()` in `loop()` watches the clock and logs the outcome once, so `mqtt.loop()`/OTA are no longer stalled by the up-to-10 s wait on every reconnect. Compile-checked (`pio run -e d1_mini`); `src/main.cpp` has no host test coverage, so unverified on hardware.
- ✅ **Standalone `onScheduled()` scheduled-read skip**: the same defect as #2, but in the MQTT firmware's independent scheduler (`src/main.cpp`): the read keyed on `tm_sec == 0`, a one-second window a stalled loop could jump. Now it fires anywhere inside the scheduled minute, guarded once-per-day by a `ScheduleManager::dateKey()` latch (which also dedups the multiple poll chains started on each reconnect). The scheduler also waits for `NTP_MIN_VALID_EPOCH` before it will match a schedule, so the unset 1970 clock cannot fire a read, and it defers instead of latching while a frequency scan owns the radio. Compile-checked; no host test coverage for `main.cpp`.
- ⏭️ **Not fixed by request:** unauthenticated OTA; the never-failing clang-format/cppcheck/dependency-check CI gates.

## How the review was performed

The repo was partitioned and reviewed in eight focused passes:

**Part 1 (firmware & integration, canonical hand-written code):**

1. Core radio/protocol: `src/core/` (CC1101 driver, RADIAN decode/parse, CRC, frame parsing)
2. Services: `src/services/` (meter reader state machine, frequency manager, scheduler, history, storage)
3. MQTT firmware + adapters: `src/main.cpp`, `src/adapters/`, `include/private.example.h`
4. ESPHome component + YAML: `ESPHOME/components/everblu_meter/` + example configs

**Part 2 (everything orbiting the code):**

5. Test suite: `test/**`, `tests/esphome/test_validators.py`
6. Tooling scripts: `scripts/*.py`, PowerShell scripts, git hook, release generators
7. CI & config: `.github/workflows/*`, `.ci/esphome/**`, `platformio.ini`, lint/format configs
8. Docs: `docs/*.md`, `README.md`, `ESPHOME/docs/*.md` (technical accuracy vs code)

**Deliberately excluded:** `ESPHOME-release/` (a *generated* copy of the component, rebuilt from `src/` by `prepare-component-release`; findings on the ESPHome component map onto it) and binary/datasheet content under `docs/datasheets/`.

## Overall assessment

The **firmware itself is solid**: frame parsing is consistently bounds-checked, string building uses bounded `snprintf`, MQTT command handlers whitelist input, JSON escaping is correct, and the scan state machine is bounded with watchdog feeding. No buffer overflows or injection vectors were found in the production C++.

The recurring theme is **drift between the code and everything around it**: one unit test encodes a bug as the expected value, several docs describe a frequency-scan algorithm that was replaced, multiple CI "gates" look protective but can never fail, and the release generator breaks silently on macOS.

---

# Part 1: Firmware & Integration

## Must-fix

| # | Severity | Location | Issue |
|---|----------|----------|-------|
| 1 | HIGH | `src/main.cpp:1346` | **Unauthenticated OTA**: `ArduinoOTA.begin()` with no `setPassword`/`setPasswordHash` anywhere. Any host on the LAN can flash arbitrary firmware = full device takeover + credential exfiltration. Add a password sourced from a new `SECRET_OTA_PASSWORD` in `private.h`, documented in `private.example.h`. |
| 2 | HIGH | `src/services/meter_reader.cpp:329` | **Scheduled daily read can be silently skipped for the whole day.** Read fires only when a schedule check (every `SCHEDULE_CHECK_INTERVAL_MS` = 500 ms) observes `tm_sec == 0`. If a blocking read/scan or an NTP step crosses `:00`, the minute latches as serviced and the read never happens, and nothing logs the miss. Fix: trigger on a minute *window* (day+hour+minute match, minute not yet serviced), drop the `tm_sec==0` requirement. |
| 3 | HIGH | `ESPHOME/example-advanced.yaml:229` | **Advanced example does not compile**: `status:` sensor has `name` but no `id: meter_status`, yet its own `on_value` automation references `id: meter_status`. Verified. Add the `id:`. |

## High-value correctness bugs

| # | Severity | Location | Issue |
|---|----------|----------|-------|
| 4 | HIGH* | `src/services/storage_abstraction.cpp:143` | **ESP8266 (non-ESPHome) storage ignores the key**: `saveFloat`/`loadFloat` always use fixed `FREQ_OFFSET_ADDR`; `hasKey` compares a hardcoded `0xABCD` and ignores key+magic. Two meters collide on address 0. Latent today (standalone build uses one key) but a trap for multi-meter on that path. The ESPHome/ESP32 path is hash-keyed and unaffected. |
| 5 | MEDIUM | `src/services/meter_history.cpp:87` | **Average monthly usage off-by-one**: `totalUsage / (monthCount + 1)`; should be `/ monthCount`. Verified. Every average is understated (halved when 1 month). See Part 2 test finding: a test locks this in. |
| 6 | MEDIUM | `src/core/cc1101.cpp:1500` | **Rejected frame published as valid 0**: the implausible-history path does `memset(&data,0,…)` which also clears `data.failure` back to `None`. Caller sees `failure==None, volume==0` and publishes a bogus zero reading. Set `data.failure = ParseRejected` before/after the memset. |
| 7 | MEDIUM | `src/services/meter_reader.cpp:117` | **No DST handling**: local time = UTC + static `timezoneOffsetMinutes`. Read time drifts 1h twice a year, potentially outside the meter's wake window for months. (Documented as a known limitation, but consider deriving from a real TZ string.) |
| 8 | MEDIUM | `ESPHOME/components/everblu_meter/everblu_meter.cpp:350` | **Inconsistent button guards**: `request_manual_read()` / `request_deep_scan()` skip the `meter_initialized_` + in-progress checks that `request_scan()` / `request_reset_frequency()` have. Deep scan can run on an unconfigured/busy radio. |
| 9 | MEDIUM | `src/core/cc1101.cpp:1145` | **RSSI→dBm can underflow `int8_t`**: raw values ~128–147 compute −129…−138, wrapping to positive dBm, which can falsely trip the "NEAR-FIELD SATURATION" heuristic for a weak signal. Return `int`/`int16_t`. |
| 10 | MEDIUM | `src/services/meter_reader.cpp:118` | Read hour/minute from config not range-checked in `MeterReader` (ScheduleManager clamps; MeterReader reimplements the math without clamping). Bad config (e.g. `read_hour=27`) → reads never fire. |

*#4 is HIGH in shape but currently latent on the standalone path only.

## Medium: HA/ESPHome idiom & MQTT

- ~~**MEDIUM** `src/main.cpp:1310`: Blocking NTP wait (up to 10 s `delay` loop) inside the MQTT connect callback; stalls `mqtt.loop()`/OTA on every reconnect. Make it non-blocking in `loop()`.~~ **RESOLVED (`fix/code-review-2026-09`).** Connect callback now kicks off `configTzTime()` and returns; `pollNtpSync()` in `loop()` polls the clock and logs the outcome once. Compile-checked; unverified on hardware (no host tests for `main.cpp`).
- **MEDIUM** `src/main.cpp:269`: MQTT is plaintext on port 1883; credentials + command topics are sniffable. Offer/document a TLS build (8883). Reasonable for local-only brokers, but state it.
- **MEDIUM** `ESPHOME/components/everblu_meter/everblu_meter.cpp:465`: `PollingComponent`/`update_interval` is exposed but `update()` is a no-op; the advanced example's `update_interval: 30s` + "fast updates" comment misleads. Drop polling or remove the comment.
- ~~**MEDIUM** `ESPHOME/components/everblu_meter/__init__.py:369`: `timestamp` text sensor uses `device_class: timestamp`, which HA requires to be strict ISO-8601 + timezone or the entity goes unavailable. Confirm the publisher's format.~~ **RESOLVED: not a defect.** The publisher formats the string with `strftime(..., "%FT%TZ", gmtime(&now))` (`meter_reader.cpp:434`), i.e. `2026-09-10T10:00:00Z`: valid ISO-8601 with a `Z` timezone designator. No change needed.
- **MEDIUM** WiFi serial telnet monitor (port 23, unauthenticated) exposes device internals when enabled. Default-off and well-warned; add an "unauthenticated port" note. (Secrets themselves are never printed.)

## Low: cleanup & minor correctness (Part 1)

- `src/services/meter_reader.cpp:249`: retry timer uses non-rollover-safe absolute `millis()` compare (cooldown does it right); misfires within 5 s of the 49-day wrap.
- `src/core/cc1101.cpp:463` / `:498`: `SPIReadBurstReg`/`SPIWriteBurstReg` take `uint8_t len`, so the `> MAX_SPI_BURST_SIZE` (1024) guard is dead code; a >255 request silently wraps. Use `uint16_t`/`size_t`.
- `src/core/radian_decoder.cpp:34`: `bit_cnt` run-length counter is `uint8_t`; a >255-sample constant run wraps (silent decode corruption; largely theoretical post-sync).
- MQTT: Scan/Stop-Scan buttons omit `avty_t` availability topic (`src/main.cpp:1219`); publish return values never checked; JSON `rssi` field uses a different source than the `rssi_dbm` topic.
- Duplicated schedule/timezone logic: `MeterReader` reimplements `ScheduleManager`'s day-matching + UTC↔local math, which is *why* the clamp bug (#10) exists in one copy only. Consolidate onto `ScheduleManager`.
- Dead/misc: `validate_meter_code` unreachable empty-serial branch (`__init__.py:200`); `frequency` validator allows out-of-band values (300–928 vs CC1101's 3 bands); `wifi_ready_at_` written-never-read; duplicate `apply_radio_context()` call; empty `if (size>=49)` block (`cc1101.cpp:1357`); debug print reads wrong offset (`66+i*4` vs data at `70+i*4`, `cc1101.cpp:1436`); unreachable register-column cases; non-ASCII `✓`/`✗` in log strings; `saveFloat`'s `delay(100)` blocks the loop on every adaptive-tracking write (also flash-wear).
- `src/services/meter_history.cpp:224`: `countValidMonths` stops at the first zero; a legitimate mid-history zero (meter reset) truncates older data. Likely acceptable; confirm.

## Confirmed clean (Part 1)

Untrusted-RF-frame security (all fields CRC-gated + bounds-checked, `meter_type` copy bounded, no frame-controlled format strings); CRC store/verify byte order consistent; scan state machine bounded with watchdog feeding; frequency register math within `int32_t`; ESPHome/ESP32 hash-keyed persistence with verify-readback; SPI/`setup_priority` ordering; boot-state publishing avoids uninitialized reads; device_class/state_class assignments; config-schema validators (GDO2 required/opt-out, duplicate-pin, framework, min-ESPHome-version).

---

# Part 2: Tests, Tooling, CI/Config, Docs

## Tests

| Severity | Location | Issue |
|----------|----------|-------|
| HIGH | `test/test_embedded_unit/test_meter_history.cpp:96` | **Asserts the buggy average as expected**: `TEST_ASSERT_EQUAL_UINT32(40, …)` with comment `// 160 / (3 + 1)`. Correct value is `160/3 = 53`. Fixing Part 1 #5 correctly will break this test. Fix both together. |
| HIGH | `src/core/cc1101.cpp:1145` (gap) | `cc1100_rssi_convert2dbm` (int8_t underflow, Part 1 #9) has **zero coverage** and isn't declared in `cc1101.h`, so it's structurally unreachable from tests. Publisher tests feed a pre-converted value. Declare it in the header, widen/clamp the type, add boundary tests (Rssi_dec 127/128/129/255). |
| MEDIUM | `test/test_native_meter_reader/test_meter_reader.cpp:395` | Scheduled-read test pins the clock at `tm_sec==0`, so it can never reproduce the missed-window skip (Part 1 #2). Add a test at `HH:MM:30` / crossing the minute on non-zero seconds. |
| MEDIUM | (gap) | MeterReader hour/minute clamp divergence (Part 1 #10) untested; only ScheduleManager's clamp is verified (`test_schedule_manager.cpp:382`). |
| MEDIUM | `test/test_embedded_unit/test_config_validation.cpp:78` | `test_frequency_validation` is a tautology: it asserts bounds on a literal constant, invokes no production validator. |

**Confirmed sound:** transmit round-trip (`test_transmit_frame.cpp`), staged/deep-scan coverage (`test_staged_scan.cpp`: every cancel path, radio-fault-never-saves), and the Python validator tests are genuinely strong. Test gaps line up almost exactly with the Part 1 bugs.

## Tooling scripts

| Severity | Location | Issue |
|----------|----------|-------|
| HIGH | `ESPHOME/prepare-component-release.sh:71` | `sed -E 's|#include\s+…'` uses `\s` (GNU-sed extension). On macOS/BSD sed it silently no-ops → generated release keeps `#include "core/foo.h"` pointing at non-existent paths → won't compile. CI is Linux, so it won't catch this. Fix: use `[[:space:]]+`. |
| MEDIUM | `scripts/capture-mqtt-log.ps1:55` | Firmware upload exit code ignored (PS 5.1 doesn't trip on native non-zero exit); a failed flash silently monitors stale firmware. Add `if ($LASTEXITCODE -ne 0) { throw }`. |
| MEDIUM | `scripts/hooks/pre-commit:16` | Runs only the `.ps1`; on Unix without PowerShell it warns and `exit 0`, committing a stale `ESPHOME-release/` even though the `.sh` sits right there. Fall back to the `.sh`. |
| MEDIUM | `scripts/extract-meter-fixture.py:120` | `validate_crc` marks truncated frames `crc_valid=1` without computing a CRC (plus a dead `missing==2` branch). Truncated captures become fixtures asserting a CRC the firmware would reject, which masks decode regressions. |
| LOW | `scripts/install-hooks.ps1:9` | Relative paths not anchored to repo root; assumes `.git` is a directory (breaks in worktrees). `.ps1`/`.sh` release generators have also drifted (`touch __pycache__` only in `.sh`; differing post-gen verification). |

**Clean:** no command-injection or secret-leak vectors; `run-tests.ps1` and `deploy-esphome-to-ha.ps1` robocopy exit-code handling are sound. `deploy-esphome-to-ha.ps1` hardcodes an internal UNC share IP, worth parameterising, not a security issue.

## CI & config

| Severity | Location | Issue |
|----------|----------|-------|
| MEDIUM | `.github/workflows/release.yml:31` | Script injection: `TAG="${{ github.event.inputs.tag }}"` interpolated into `run:` with `contents: write`. Use `env:` + `"$TAG_INPUT"` (the pattern `esphome-external-component.yml` already uses correctly). |
| MEDIUM | `.github/workflows/codeql.yml:26` | CodeQL scans **only Python**; the security-sensitive C/C++ (radio framing, RADIAN parse, CRC, buffers) is unscanned, and it runs only on push/schedule, not PRs. Enable the commented `c-cpp` build or document the exclusion. |
| MEDIUM | `.github/workflows/dependency-check.yml:52` | `safety check \|\| true` + `continue-on-error`: the vulnerability-scan job can never fail on a CVE. Also holds an unused `security-events: write`. |
| MEDIUM | `.github/workflows/code-quality.yml:154` | clang-format check ends in `\|\| true`; cppcheck uses `--error-exitcode=0`. Both "gates" never fail. (Possibly intentional since pre-commit.ci enforces; if so, rename them.) |
| MEDIUM | `.github/workflows/toc.yml:24` | Third-party actions pinned to mutable tags (`toc-generator@v4` with `contents: write`, `action-gh-release@v1`, `codecov-action@v5` with a token). Pin to commit SHAs. |
| LOW | `.github/workflows/matchers/gcc.json:8` | Regex strips the `src/` prefix, so GCC annotations don't link to files; `include/` paths never match. |
| LOW | `ruff.toml:7`, `.yamllint:9` | Comments cite a non-existent `.github/workflows/lint.yml`. |
| LOW | `.ci/.../test.esp8266-git.yaml:41` | Hardcodes the upstream repo URL, so the git smoke test validates upstream (not a fork) at `${git_ref}`. All fixtures set `min_version: 2026.1.0` while workflows install `esphome>=2025.0`. |

**Confirmed sound (important):** `esphome-release-sync-check.yml` genuinely catches a stale release tree (regenerates via `.sh`, fails on any diff under `ESPHOME-release/`): but only on PRs/dispatch, not push. All five `test.invalid-*.yaml` negative fixtures violate exactly what their filename claims *and* the job asserts both non-zero exit and the expected substring, so they can't false-pass. No `pull_request_target` misuse; all workflows declare `permissions:`.

## Docs (technical accuracy)

| Severity | Location | Issue |
|----------|----------|-------|
| HIGH | `ESPHOME/docs/ESPHOME_INTEGRATION_GUIDE.md:170` | `auto_scan` documented as default `true`; code default is `False` (`__init__.py:244`). README correctly says `false`: the two disagree. |
| HIGH | `docs/ADAPTIVE_FREQUENCY_FEATURES.md` | Broadly obsolete: describes a `performWideInitialScan()` that doesn't exist, `FOCCFG=0x1D` (code is `0x1E`), adaptive threshold "10" (now 1), removed MQTT topic `frequency_scan`. Mark as historical or rewrite. |
| HIGH | `docs/QUICK_REFERENCE.md:83` | Scan "±30 kHz, 5 kHz step, best RSSI"; actual is ±150/±20 kHz ranked by decode success then \|FREQEST\| (RSSI explicitly abandoned, issue #104). Also says `frequency_offset` is MHz (it's kHz) and offset limit ±0.1 MHz (actual ±0.15). |
| MEDIUM | `README.md:314` | Claims an automatic wide scan runs on first boot "by default" and the ~2 min delay "is normal", but `AUTO_SCAN_ENABLED` ships `0`. Sets wrong expectations. |
| MEDIUM | `ESPHOME/docs/DEVELOPER_GUIDE.md:356` | References removed `FrequencyManager::performFrequencyScan(...)`, "±100 kHz" limit, adaptive default 10. |
| LOW | `docs/IMPROVEMENTS_SUMMARY.md`, `docs/API_DOCUMENTATION.md:578`, `docs/CRITICAL_FIXES_APPLIED.md` | Repeat the stale scan/offset figures and removed `frequency_scan` topic; lower priority as change-log/history files. |

**Confirmed accurate:** the DST/fixed-offset limitation is well and consistently documented (`ESPHOME/README.md`, integration guide); GDO2 requirement, the multi-meter frequency-scan step math (9.918/2.380/0.793 kHz, ±150/±20 kHz, 2-of-3 verification), sensor/button lists, and all "3.3V only, not 5V" wiring warnings match the code. No dangerous advice found.

---

# Prioritized fix list

Highest-leverage fixes, in order:

1. **OTA password** (P1 #1): the one genuine security hole.
2. **`monthCount` average + its test** (P1 #5 / test finding): fix the code and flip the assertion to `53` in the same commit.
3. **Skipped-daily-read scheduler bug** (P1 #2): plus add the missed-window test.
4. **`example-advanced.yaml` `id: meter_status`** (P1 #3): broken example.
5. **`prepare-component-release.sh` `\s` → `[[:space:]]+`**: silent broken release on macOS.
6. **Doc corrections**: `auto_scan` default; mark the three superseded frequency docs as historical.

Then, as capacity allows: MQTT NTP de-blocking (P1), CodeQL C/C++ coverage, `release.yml` injection hardening, button-guard consistency (P1 #8), RSSI dBm type widening + tests (P1 #9), and the MeterReader/ScheduleManager consolidation (P1 #10).

## Notes for future reviews

- `ESPHOME-release/` is **generated** from `src/` + the ESPHome component by `prepare-component-release.{sh,ps1}`. Do not review or edit it directly; review the sources and regenerate.
- The code is strong; drift lives in tests, docs, CI gates, and the release generator. Focus review effort there.
