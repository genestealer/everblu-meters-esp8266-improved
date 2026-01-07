# Changelog

All notable changes to this project will be documented in this file.

The release process is automated via GitHub Actions and is triggered by tags matching `v*.*.*`. See `.github/workflows/release.yml`.

## [v1.1.7] - 2025-01-07

What's changed since v1.1.6:

### Fixed
- Automatically append METER_SERIAL to MQTT client ID to prevent conflicts when running multiple devices on the same broker (#35)
- Restored Home Assistant button discovery by removing unsupported per-button availability payloads
- Device now gracefully handles missing CC1101 radio without continuous reboots
- CC1101 State sensor now shows "unavailable" instead of "Idle" when radio is not connected
- WiFi and MQTT functionality maintained even when CC1101 radio is disconnected

## [v1.1.6] - 2026-01-07

What's changed since v1.1.5:

- Add support for gas meters in addition to water meters (Issue #32)
- Add `METER_TYPE` configuration option to select between water (default) or gas meters
- Gas meter readings automatically convert to cubic meters (m³) with configurable divisor
- Add WiFi-based TCP serial monitor for remote debugging via Telnet (disabled by default)
- Add support for multiple board environments in OTA configuration
- Add OTA update support for all boards with intelligent device boot detection
- Add `MAX_RETRIES` configuration option for customizable retry behavior
- Fix auto-alignment to only apply to scheduled reads, not manual MQTT reads (Issue #34)
- Update MQTT discovery and Home Assistant integration for both meter types
- Improve logging with consistent context prefixes for easier debugging
- Rename MQTT entities from `water_meter_*` to `everblu_meter_*` for clarity
- Update documentation with comprehensive setup and configuration guides

## [v1.1.5] - 2025-11-18

What's changed since v1.1.4:

- Address Copilot AI review feedback
- Fix critical VLA stack overflow bug causing data corruption (Issue #20)
- Add `.txt` versions of datasheets
- Document dredzik improvement experiments and validate 4x oversampling strategy
- Add error handling for invalid `reads_counter` values during meter report parsing

## [v1.1.4] - 2025-11-18

What's changed since v1.1.3:

- Fix MQTT discovery JSON construction issues and improve overall code quality
- Fix MQTT discovery for water meter reading sensor
- Improve MQTT topic initialization for meter serial handling
- Add optional MQTT debugging output
- Prefix MQTT topics and entity IDs with `METER_SERIAL` to support multi-meter deployments

## [v1.1.3] - 2025-11-18

What's changed since v1.1.2:

- Enhance RADIAN CRC validation to handle frame length discrepancies
- Add CRC-16/KERMIT verification for RADIAN frames
- Add CRC validation support for RADIAN protocol frames in the CC1101 interface
- Improve error logging around CRC failures

## [v1.1.2] - 2025-11-18

What's changed since v1.1.1:

- Implement automatic wide-frequency scan control on first boot
- Add functions to update and validate reading schedule times using local and UTC time
- Fix multiple definition of `validateConfiguration` (Issue #22)
- Clean up formatting and improve readability of `getch` function
- Add configuration validation helpers with corresponding unit tests
- Add comprehensive Doxygen-style API documentation and `API_DOCUMENTATION.md`

## [v1.1.1] - 2025-11-17

What's changed since v1.1.0:

- Add additional plausibility checks for parsed meter readings (liters) to reject clearly impossible totals before publishing
- Harden historical volume validation to discard inconsistent history blocks while keeping valid primary meter fields
- Make historical attributes JSON generation more robust by using a larger buffer, tracking remaining space, and avoiding malformed payloads on truncation

## [v1.1.0] - 2025-11-17

What's changed since v1.0.1:

- Add first-layer protection against corrupted RADIAN frames using decode-quality and plausibility checks
- Prevent obviously invalid meter data from being published to MQTT
- Surface firmware version (`EVERBLU_FW_VERSION` from `version.h`) as Home Assistant device software version via MQTT discovery
- Add retry handling, frequency scan notifications, and connectivity watchdog
- Add timezone offset support
- Add datasheets to the repository

## [v1.0.1] - 2025-11-01

What's changed since v1.0.0:

- Add WeMos D1 Mini board configuration to release workflow
- Make decoder tolerant to stop-bit errors and fix FALSE macro usage
- Add CC1101/RADIAN debug output control to `private.h` and `cc1101.cpp`
- Rename `config.h` to `private.h` and update related documentation
- Update `.gitignore`

## [v1.0.0] - 2025-10-30

- Initial tagged release

---

Format inspired by Keep a Changelog. For full details, see the Git commit history and GitHub Releases.
