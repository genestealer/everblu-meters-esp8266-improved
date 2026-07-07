# Breaking Changes - v3.1.0

This release includes integration-level breaking changes for ESPHome and MQTT scan naming/behavior.

## ESPHome Configuration Migration

Update scan button keys in YAML:

- `frequency_scan_button` -> `deep_scan_button`
- `wide_frequency_scan_button` -> `deep_scan_button`
- `fast_scan_button` -> removed (no direct replacement)

Notes:

- `auto_scan` default changed to `false` (startup deep scan is now opt-in).
- `auto_scan_on_failure` remains available for recovery scans after repeated failures.

## MQTT Topic/Automation Migration

Update scan command topic references:

- `freq_scan` -> `deep_scan`
- `fast_scan` -> removed

If Home Assistant automations or scripts reference old entities/topics, update them to the `deep_scan` equivalents.

## Why This Is Breaking

Existing YAML keys and MQTT topic names used by prior versions will no longer trigger scan actions unless migrated.

## Quick Checklist

1. Replace old ESPHome scan button keys with `deep_scan_button`.
2. Replace MQTT `freq_scan` usage with `deep_scan`.
3. Remove any `fast_scan` references.
4. Decide whether to explicitly set `auto_scan: true` for startup behavior matching older installs.

## Release Summary For Reviewers (v3.1.0)

### What Is Changing

- Bumps release version metadata to `3.1.0`.
- Finalizes release notes in `CHANGELOG.md` for the `develop -> main` delta.
- Adds explicit AI-oriented release metadata blocks to improve future tooling/auditability.
- Audits branch history so changelog coverage includes merged work and superseded iterations.
- Enriches historical major/minor release metadata on `main` using PR-description context.

### Main Behavior And Feature Changes Called Out In This Release

- Frequency calibration/scanning improvements:
	- Two-phase Deep scan behavior and quality guard hardening.
	- Optional `AUTO_SCAN_ON_FAILURE_ENABLED` flow with immediate retry after successful retune.
	- Default startup auto-scan disabled (`AUTO_SCAN_ENABLED`/`auto_scan` -> false).
	- CC1101 RX bandwidth widened (58 kHz -> 270 kHz) with related FOC changes.
- MQTT/telemetry updates:
	- Reset Frequency Offset button/topic in standalone MQTT.
	- `tuned_frequency` and `frequency_estimate` sensors discovery + publishing fixes.
	- Timestamped tagged serial log lines.
- Decoder/data quality:
	- RADIAN decoder deduplication to shared implementation.
	- Reading plausibility guard vs history.
	- LQI/CRC-bit handling and percentage mapping fixes.
- RF/runtime hardening:
	- Near-field saturation detection warning.
	- RX mode timeout/recovery in CC1101 state handling.
	- AGC profile rebalance and attenuation options.
- History payload correction:
	- `monthly_usage` omits oldest month lacking prior delta baseline.

### Linked PRs Included In This Release Branch

- #105
- #106
- #111
- #117
- #119
- #120
- #121
- #124

### Related Issues Referenced By Release Notes/Work

- #104 (deep scan quality ranking / non-regression)
- #109 (CC1101 AGC profile)
- #110 (shared FrequencyManager refactor)
- #118 (RADIAN decoder deduplication)

### Note On Superseded Work

- Earlier manual wide-scan UX/topic naming work is superseded by final Deep-scan naming/behavior documented in the changelog.
