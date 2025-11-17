# Changelog

All notable changes to this project will be documented in this file.

The release process is automated via GitHub Actions and is triggered by tags matching `v*.*.*`. See `.github/workflows/release.yml`.

## [v1.1.1] - 2025-11-17

What's changed since v1.1.0:

- Add additional plausibility checks for parsed meter readings (liters) to reject clearly impossible totals before publishing
- Harden historical volume validation to discard inconsistent history blocks while keeping valid primary meter fields
- Make historical attributes JSON generation more robust by using a larger buffer, tracking remaining space, and avoiding malformed payloads on truncation

## [v1.1.0] - 2025-11-17

What's changed since v1.0.1:

- Add first-layer protection against corrupted RADIAN frames (decode-quality and plausibility checks)
- Prevent obviously invalid meter data from being published to MQTT
- Surface firmware version (`EVERBLU_FW_VERSION` from `version.h`) as Home Assistant device software version via MQTT discovery

## [v1.0.1] - 2025-11-01

What's changed since v1.0.0:

- Add WeMos D1 Mini board configuration to release workflow
- Revert "Add WeMos D1 Mini board configuration to release workflow"
- Add WeMos D1 Mini board configuration to release workflow
- Added D1 Mini
- Fixd FALSE macro and make decoder tolerant to stop-bit errors
- Add CC1101/RADIAN debug output control to private.h and cc1101.cpp
- Rename config.h to private.h and update related documentation
- Delete
- Update .gitignore

## [v1.0.0] - 2025-10-??

- Initial tagged release

---

Format inspired by Keep a Changelog. For full details, see the Git commit history and GitHub Releases.
