# Up-Reving Process (Release Management)

This document outlines the process for versioning and releasing the EverBlu Meters project, which maintains both a standalone PlatformIO firmware and an ESPHome external component.

**Version Format**: Semantic Versioning (MAJOR.MINOR.PATCH), e.g., `v2.1.0`

---

## Table of Contents

1. [Pre-Release Checklist](#pre-release-checklist)
2. [Version Update Steps](#version-update-steps)
3. [Changelog Management](#changelog-management)
4. [Testing & Validation](#testing--validation)
5. [Git Tagging & Release](#git-tagging--release)
6. [Post-Release Tasks](#post-release-tasks)
7. [Release Packages](#release-packages)
8. [Rollback Procedure](#rollback-procedure)

---

## Pre-Release Checklist

Before incrementing the version, verify the following:

- [ ] All features/fixes intended for this release are merged to `main`
- [ ] All automated tests pass (GitHub Actions workflows)
  - [ ] ESP8266 build (`build-esp8266.yml`)
  - [ ] ESP32 build (`build-esp32.yml`)
  - [ ] Code quality analysis (`code-quality.yml`)
  - [ ] Memory & size tracking (`memory-and-size.yml`)
  - [ ] Config validation (`config-validation.yml`)
  - [ ] Dependency check (`dependency-check.yml`)
- [ ] Manual hardware testing completed on representative boards
  - [ ] Water meter reading accuracy
  - [ ] Gas meter reading accuracy
  - [ ] ESPHome component integration
  - [ ] MQTT standalone mode
  - [ ] OTA updates (if applicable)
- [ ] Documentation is current and accurate
- [ ] No critical open issues blocking the release
- [ ] Code review and QA sign-off complete

---

## Version Update Steps

### Step 1: Determine Version Number

Use semantic versioning:
- **MAJOR**: Breaking changes, incompatible API changes, major feature rewrites
- **MINOR**: New features, non-breaking additions, significant enhancements
- **PATCH**: Bug fixes, documentation updates, minor improvements

#### Example Version Progression:
```
v2.0.0 → v2.1.0 (new ESPHome integration)
v2.1.0 → v2.1.1 (bug fix)
v2.1.1 → v3.0.0 (breaking API changes)
```

### Step 2: Update Firmware Version Header

Edit [src/core/version.h](../src/core/version.h):

```cpp
// Firmware version definition
// Keep this in sync with the latest entry in CHANGELOG.md and Git tags.

#ifndef EVERBLU_FW_VERSION
#define EVERBLU_FW_VERSION "X.Y.Z"  // ← Update this
#endif
```

**Important**: This file is the single source of truth for the firmware version. All other references are compile-time strings pulled from this definition.

### Step 3: Update CHANGELOG

Edit [CHANGELOG.md](../CHANGELOG.md):

1. Add a new section at the **top** under "## Changelog":

```markdown
## [vX.Y.Z] - YYYY-MM-DD

### Added
- Brief description of new features

### Changed
- Brief description of modifications

### Fixed
- Brief description of bug fixes

### Known Issues
- Any breaking changes or limitations

### Testing
- What was tested and on what platforms/hardware

### Security
- Any security-related changes (if applicable)
```

2. Use these standard sections in this order:
   - **Added**: New features
   - **Changed**: Changes to existing functionality
   - **Fixed**: Bug fixes
   - **Removed**: Removed features (rarely used)
   - **Deprecated**: Deprecated features (rarely used)
   - **Security**: Security-related fixes
   - **Known Issues**: Limitations or breaking changes
   - **Testing**: Summary of testing performed

3. Be concise and user-focused. Examples:
   - ✅ Good: "Fixed frequency offset persistence across ESPHome reboots"
   - ❌ Bad: "Modified the offset storage mechanism in RTC memory"

### Step 4: Verify All References Are Updated

Check that version information is consistent across files:

```powershell
# Windows PowerShell - check all version references
$version = "X.Y.Z"
Select-String -Path src/core/version.h, CHANGELOG.md, README.md -Pattern $version
```

Common places version appears:
- [src/core/version.h](../src/core/version.h) — **PRIMARY SOURCE**
- [CHANGELOG.md](../CHANGELOG.md) — Changelog entry
- [README.md](../README.md) — Release badge and feature descriptions
- [ESPHOME/README.md](../ESPHOME/README.md) — ESPHome feature descriptions (optional)
- GitHub Release notes (created after tagging)

---

## Changelog Management

### Changelog Best Practices

1. **Timing**: Update CHANGELOG.md BEFORE tagging. Never update after the tag.

2. **User-Focused Language**: Write for end users, not developers.
   - ✅ "Added automatic frequency offset discovery on startup"
   - ❌ "Refactored the offset_discovery() function"

3. **Link to Issues/PRs** (optional but recommended):
   ```markdown
   ### Fixed
   - Fixed ESPHome radio initialization ([#42](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/42))
   ```

4. **Breaking Changes Highlighted**:
   ```markdown
   ### ⚠️ Breaking Changes
   - Removed `MQTT_AUTO_RECONNECT` configuration option (now automatic)
   - Minimum PlatformIO version bumped to 6.0.0
   ```

5. **Maintain Changelog History**: Never delete old entries. Full history should be searchable.

### Example Changelog Entry

```markdown
## [v2.2.0] - 2026-02-15

### Added
- **Dual-mode release packages**: `ESPHOME-release/` directory now matches mainline code
- **ESPHome button controls**: Manual read trigger, frequency reset, radio reinitialization
- **Enhanced logging**: Detailed frequency adjustment and read attempt tracking
- Support for gas meters with custom volume divisor configuration

### Changed
- Improved ESPHome sensor data publishing with new `read_attempts` counter
- Updated YAML configuration examples for clarity
- Enhanced `adapters/` structure for better code organization

### Fixed
- ESPHome radio initialization on startup
- Log routing in ESPHome UI showing MeterReader output
- Frequency offset persistence between reads

### Known Issues
- CC1101 best frequency not persisted between ESPHome reboots (design limitation)
- MQTT mode requires private.h configuration for meter settings

### Testing
- Fully tested on ESP8266 and ESP32 hardware
- Validation with water and gas meters
- ~95% code reuse between MQTT and ESPHome modes
```

---

## Testing & Validation

### Local Build Testing

Before releasing, build and test locally:

```powershell
# Test PlatformIO build (huzzah board)
pio run

# Test all known board configurations
pio run --environment huzzah
pio run --environment huzzah-ota
pio run --environment nodemcu
# (add other environments as applicable)
```

### ESPHome Component Testing

```powershell
# 1. Run the release build script
.\ESPHOME\prepare-component-release.ps1

# 2. Verify ESPHOME-release directory is clean and properly formatted
Get-ChildItem ESPHOME-release -Recurse | Measure-Object

# 3. Manually test in ESPHome environment:
#    - Create test YAML configuration
#    - Validate schema
#    - Compile for multiple boards (huzzah, nodemcu, esp32)
#    - Flash to test hardware
#    - Verify all sensors and buttons work
```

### Hardware Testing Checklist

- [ ] Water meter test (verify reading accuracy)
- [ ] Gas meter test (verify reading accuracy)
- [ ] Frequency scanning and discovery
- [ ] MQTT connectivity and message publishing
- [ ] ESPHome Home Assistant integration
- [ ] Button controls (read trigger, frequency reset, etc.)
- [ ] OTA updates (if applicable)
- [ ] Log output clarity and diagnostic information
- [ ] Power consumption (if baseline exists)
- [ ] Stability under extended operation (24+ hours)

### GitHub Actions Validation

All automated tests must pass:

1. Navigate to [GitHub Actions](https://github.com/genestealer/everblu-meters-esp8266-improved/actions)
2. Verify latest build on `main` passed
3. Check each workflow:
   - ✅ `build-esp8266.yml`
   - ✅ `build-esp32.yml`
   - ✅ `code-quality.yml`
   - ✅ `memory-and-size.yml`
   - ✅ `config-validation.yml`
   - ✅ `dependency-check.yml`

---

## Git Tagging & Release

### Step 1: Commit Changes

```powershell
# Stage all updated files
git add src/core/version.h CHANGELOG.md README.md

# Commit with clear message
git commit -m "chore: bump version to v2.2.0

- Updated firmware version in version.h
- Added comprehensive CHANGELOG entry
- Updated README with new features"
```

### Step 2: Create Annotated Git Tag

```powershell
# Create the tag with a message
git tag -a v2.2.0 -m "Release v2.2.0

ESPHome integration enhancements
- Added button controls for manual operations
- Improved frequency offset discovery
- Enhanced logging and diagnostics

See CHANGELOG.md for full details"
```

**Tag Format Rules**:
- Tags MUST start with `v` (e.g., `v2.2.0`)
- Use semantic versioning (MAJOR.MINOR.PATCH)
- Tag message should reference CHANGELOG
- Use annotated tags (with `-a` flag), not lightweight tags

### Step 3: Push Tag to Remote

```powershell
# Push the commit
git push origin main

# Push the tag
git push origin v2.2.0

# Verify tag is on remote
git ls-remote origin v2.2.0
```

### Step 4: Create GitHub Release

The tag push to GitHub automatically creates a release. You can enhance it:

1. Navigate to [Releases](https://github.com/genestealer/everblu-meters-esp8266-improved/releases)
2. Find the newly created release for `v2.2.0`
3. Edit the release notes:
   - Copy relevant sections from CHANGELOG.md
   - Add special notes (e.g., "This is a major update...")
   - Mark as **pre-release** if applicable
   - Set as **latest release** when ready

**Release Notes Template**:
```markdown
# Release vX.Y.Z

**Release Date**: YYYY-MM-DD

## Summary
One-line summary of main focus (e.g., ESPHome integration or security patches)

## Installation
- **MQTT Mode**: [Build and flash](#) from source with PlatformIO
- **ESPHome Mode**: Use `external_components` with [ESPHOME-release/](ESPHOME-release/) component

## What's New
[Paste from CHANGELOG.md "## Added" and "## Changed" sections]

## Bug Fixes
[Paste from CHANGELOG.md "## Fixed" section]

## Known Issues
[Paste from CHANGELOG.md "## Known Issues" section]

## Assets
- `main` branch at tag `v2.2.0`
- `ESPHOME-release/` component (generated by release build script)

## Upgrade Instructions
[Add specific instructions if applicable, e.g., breaking changes, config updates]
```

---

## Post-Release Tasks

### Update Development Version (Optional)

Some projects bump to next development version immediately after release:

```powershell
# Edit src/core/version.h
# Change "2.2.0" to "2.2.1-dev" (next patch version with -dev suffix)

git add src/core/version.h
git commit -m "chore: bump to development version v2.2.1-dev"
git push origin main
```

This is optional but helps identify development builds in logs.

### Prepare Release Package

```powershell
# Run the ESPHome release build script (already done during testing)
.\ESPHOME\prepare-component-release.ps1

# Verify ESPHOME-release directory structure
# This is automatically included in the Git release

# Optionally create a binary release artifact:
# - Archive `ESPHOME-release/` as `everblu_meter-v2.2.0.zip`
# - Attach to GitHub release for direct download
```

### Announce Release

- [ ] Update project documentation links if needed
- [ ] Post to Home Assistant forums/Discord if major features added
- [ ] Notify users if breaking changes present
- [ ] Update any external integrations (if applicable)

---

## Release Packages

### MQTT Firmware Package (Standalone)

**Location**: Git repository at tag `vX.Y.Z`

**Contents**:
- `src/` — Source code
- `include/` — Configuration headers
- `platformio.ini` — Build configuration
- `CHANGELOG.md` — Release notes
- `README.md` — Documentation

**Distribution**: Users clone or download from GitHub, configure `private.h`, build with PlatformIO

### ESPHome Component Package

**Location**: `ESPHOME-release/everblu_meter/` (generated)

**Contents** (generated by `prepare-component-release.ps1`):
- Flattened source tree (all headers and sources in one directory)
- ESPHome Python integration (`__init__.py`)
- Component-specific files (`everblu_meter.h`, `everblu_meter.cpp`)
- `DO_NOT_EDIT.md` warning marker

**Distribution**: 
- Included in Git release at `vX.Y.Z`
- Users reference `external_components` in ESPHome YAML with version tag
- Version tag matches firmware version in `version.h`

**Build Command** (Users):
```yaml
external_components:
  - source:
      type: git
      url: https://github.com/genestealer/everblu-meters-esp8266-improved
      ref: v2.2.0
    components:
      - everblu_meter
```

---

## Rollback Procedure

If a release has critical issues and needs to be yanked:

### Option 1: Delete Tag (Immediate Removal)

```powershell
# Delete local tag
git tag -d v2.2.0

# Delete remote tag
git push origin --delete v2.2.0

# Verify deletion
git tag -l | grep v2.2.0  # Should return nothing
```

### Option 2: Mark as Pre-Release (Keep Tag)

If some users already downloaded:

1. Edit GitHub release
2. Check **"This is a pre-release"** box
3. Add prominent warning to release notes:
   ```
   ⚠️ **YANKED** - This release has critical issues.
   Please use vX.Y.Z instead.
   ```

### Option 3: Release a Patch Version

If issues are fixed quickly:

```powershell
# Fix the issues
# Update version to vX.Y.Z+1
# Update CHANGELOG with "## Fixed" section
# Tag as vX.Y.Z+1
# Mark vX.Y.Z as pre-release with warning
```

---

## Version Numbering Quick Reference

| Scenario | Old Version | New Version | Type |
|----------|-------------|-------------|------|
| New features (backward compatible) | v2.0.5 | v2.1.0 | MINOR |
| Bug fixes only | v2.0.5 | v2.0.6 | PATCH |
| Breaking API changes | v2.1.0 | v3.0.0 | MAJOR |
| Major refactoring | v2.1.0 | v3.0.0 | MAJOR |
| Performance improvements | v2.0.5 | v2.1.0 | MINOR |
| Documentation updates | v2.0.5 | v2.0.6 | PATCH |
| ESPHome integration launch | v2.0.x | v2.1.0 | MINOR |

---

## Useful Commands Reference

```powershell
# View all tags
git tag -l

# View tag details
git show v2.1.0

# View commits since last tag
git log v2.0.0..v2.1.0 --oneline

# Create tag from past commit
git tag -a v2.1.0 <commit-hash> -m "Release message"

# Delete local tag
git tag -d v2.1.0

# Delete remote tag
git push origin --delete v2.1.0

# Verify tag signature (if using signed tags)
git tag -v v2.1.0
```

---

## Summary Workflow

```
1. Develop features/fixes on branches → PR → Review → Merge to main
   ↓
2. All tests pass on main (GitHub Actions)
   ↓
3. Decide on version number (MAJOR.MINOR.PATCH)
   ↓
4. Update src/core/version.h with new version
   ↓
5. Update CHANGELOG.md with features/fixes/notes
   ↓
6. Local build testing (pio run, test all environments)
   ↓
7. Hardware testing (water/gas meters, MQTT, ESPHome)
   ↓
8. Run ESPHome release build script (prepare-component-release.ps1)
   ↓
9. git commit version updates
   ↓
10. git tag -a vX.Y.Z -m "Release message"
   ↓
11. git push && git push origin vX.Y.Z
   ↓
12. Create/edit GitHub Release notes
   ↓
13. Announce release
```

---

## Notes

- **Single Source of Truth**: `src/core/version.h` is the authoritative version. All other references are derived from compile-time strings.
- **Never Modify After Tag**: Once tagged and released, the code for that tag should never change. Create a new patch version instead.
- **Dual-Mode Versioning**: MQTT and ESPHome share the same version number (defined in `version.h`). Both are released simultaneously.
- **Breaking Changes**: Always document in CHANGELOG and create upgrade instructions in GitHub release.
