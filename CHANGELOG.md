# Changelog

All notable changes to this project will be documented in this file.

Releases are created manually by tagging commits with version tags matching `v*.*.*` (e.g., `v2.1.0`). Users should build from source and configure `private.h` with their own meter settings.

## [Unreleased]

### Fixed

- Schedule logic bug where "Monday-Sunday" schedule excluded Sunday
- ESPHome component memory leak from raw `new` allocations without destructor
- Schedule constant mismatch in ESPHome Python code (removed "Everyday", "Saturday", "Sunday")
- Duplicate STRINGIFY macro definition with include guards
- ESPHome namespace compliance for `setup_priority` constant

### Added

- Comprehensive unit tests for schedule manager covering all day combinations
- Cross-platform Git hook installation with automatic executable bit setting
- PowerShell executable detection in pre-commit hook with graceful fallback

### Changed

- Updated schedule documentation to use standardized names
- Updated ESPHome testing checklist to reflect current schedule options

### Removed

- `SCHEDULE_EVERYDAY` constant - use `SCHEDULE_MONDAY_SUNDAY` instead
- References to legacy "Everyday", "Saturday", "Sunday" schedule options

## [v2.1.0] - 2026-01-19

**🎉 ESPHome Integration is NOW FULLY WORKING! 🎉**

Major production release with fully functional ESPHome component integration.

### Added

- **ESPHome Integration**: Full production support with 15+ sensors, Home Assistant binary state gating, and button controls
- **Adaptive Frequency Tracking**: Intelligent frequency offset optimization based on successful reads with configurable thresholds
- **Enhanced Sensors**: Read attempts counter, frequency offset monitoring, tuned frequency reporting, and meter status indicators
- **Dual-Mode Release Package**: `ESPHOME-release/` directory for ESPHome distribution, dynamically built from shared source
- **Release Build Scripts**: Cross-platform PowerShell and bash scripts to prepare component distribution
- **Storage Persistence**: Frequency offset persistence with verification (note: offset discovery not persistent between ESPHome reboots)
- **Button Controls**: Manual read triggers, frequency offset reset, and radio reinitialization commands

### Changed

- Updated YAML configuration examples (advanced, minimal, water meter) with new sensors and features
- Enhanced data publishing in both ESPHome and MQTT adapters for new metrics
- Improved logging for frequency adjustments and read attempts
- IntelliSense optimization for ESPHome component development
- Documentation and repository URL updates for release clarity

### Fixed

- ESPHome radio initialization and sensor availability handling
- Log routing in ESPHome to capture all MeterReader logs in UI
- Repository URL references in documentation
- VSCode configuration for ESPHOME-release folder integration

### Known Issues

- CC1101 discovered best frequency not persistent between ESPHome reboots (frequency offset offset discovery mechanism needs refinement)
- All core functionality remains operational despite this limitation

### Testing

- ESPHome integration fully tested on hardware
- Standalone MQTT mode validated on ESP8266
- Dual-mode shared codebase verified with ~95% code reuse

### Dual-Mode Architecture

The `ESPHOME-release/` folder is **dynamically built** from shared source files via `prepare-component-release.ps1` (Windows) or `prepare-component-release.sh` (Unix):

1. **Single Source, Dual Distribution**: Core business logic in `src/` is shared between standalone MQTT and ESPHome modes
2. **Release Preparation**: Build scripts copy entire `src/` tree + ESPHome adapter layer into `ESPHOME-release/everblu_meter/`
3. **Include Path Preservation**: No file modifications-pure structural copy preserves all relative includes
4. **Component Integration**: ESPHome loads all `.cpp`/`.h` files directly from the release package
5. **Maintenance Simplification**: Bug fixes and features in `src/` automatically included in next component distribution

This approach eliminates code duplication while supporting both MQTT discovery and native ESPHome integration seamlessly.

## [v2.0.0] - 2026-01-08

Major architectural refactor with ESPHome integration support.

### Added

- **ESPHome Integration** (⚠️ UNTESTED): Custom component with 15+ sensors, example YAMLs, and documentation
- **Dependency Injection Pattern**: Abstract interfaces (IConfigProvider, ITimeProvider, IDataPublisher) for platform-agnostic operation
- **Dual-Mode Support**: Choose between standalone MQTT or ESPHome with ~95% code sharing
- WiFi Serial Monitor support for remote debugging

### Changed

- Reorganized code structure: `adapters/`, `core/`, `services/` directories
- MeterReader now platform-agnostic using dependency injection
- Improved separation of concerns and maintainability

### Fixed

- Missing includes causing compilation failures
- WiFi Serial Monitor now captures all MeterReader logs
- ESP32 build error with watchdog timer include

### Testing

- ✅ Standalone MQTT mode tested on ESP8266
- ⚠️ ESPHome integration UNTESTED - use with caution

### Breaking Changes

- Source code structure reorganized
- Internal APIs changed due to adapter pattern

⚠️ **Note**: ESPHome component is untested. Standalone MQTT mode validated on hardware.

## [v1.2.0] - 2026-01-07

- Major release version bump.

## [v1.1.7] - 2025-01-07

What's changed since v1.1.6:

### Fixed in v1.1.6

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
