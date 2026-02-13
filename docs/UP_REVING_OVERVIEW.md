# Up-Reving Process Overview

Complete guide for versioning and releasing the EverBlu Meters project. This document provides an overview and links to detailed resources.

---

## Quick Start

**In a hurry?** Use this 5-minute quick start:

### Windows (PowerShell)
```powershell
# 1. Prepare version update
.\prepare-release.ps1 -Version v2.2.0

# 2. Edit CHANGELOG.md with your changes
notepad CHANGELOG.md

# 3. Build and test
pio run
.\ESPHOME\prepare-component-release.ps1

# 4. Commit and tag (copy from terminal output)
git add src/core/version.h CHANGELOG.md
git commit -m "chore: bump version to v2.2.0"
git tag -a v2.2.0 -m "Release v2.2.0"
git push origin main
git push origin v2.2.0
```

### Linux/macOS (Bash)
```bash
# 1. Prepare version update
./prepare-release.sh v2.2.0

# 2. Edit CHANGELOG.md with your changes
nano CHANGELOG.md

# 3. Build and test
pio run
bash ESPHOME/prepare-component-release.sh

# 4. Commit and tag (copy from terminal output)
git add src/core/version.h CHANGELOG.md
git commit -m "chore: bump version to v2.2.0"
git tag -a v2.2.0 -m "Release v2.2.0"
git push origin main
git push origin v2.2.0
```

---

## Documentation

### [RELEASE_PROCESS.md](RELEASE_PROCESS.md) 📖
**Comprehensive guide** to the entire release process. Read this to understand:
- Pre-release validation checklist
- How versioning works (semantic versioning)
- Where version numbers are defined
- Testing requirements
- Git tagging best practices
- Rollback procedures

**When to use**: First time release or need detailed understanding

---

### [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) ✅
**Step-by-step checklist** for executing a release. Use this as your guide during release work:
- Quick yes/no checkboxes
- Commands to run at each phase
- Success criteria
- Quick rollback reference

**When to use**: During actual release execution

---

## Automation Scripts

### [prepare-release.ps1](../prepare-release.ps1) 🪟
Windows PowerShell script that automates version preparation:
```powershell
.\prepare-release.ps1 -Version v2.2.0
```

**What it does**:
- ✅ Validates you're in the correct directory
- ✅ Checks git working directory is clean
- ✅ Updates `src/core/version.h`
- ✅ Runs `prepare-component-release.ps1` (ESPHome)
- ✅ Shows git diff for verification
- ✅ Displays next steps

---

### [prepare-release.sh](../prepare-release.sh) 🐧
Linux/macOS Bash script (cross-platform):
```bash
./prepare-release.sh v2.2.0
```

**What it does**: Same as PowerShell version, bash compatible

---

### [ESPHOME/prepare-component-release.ps1](../ESPHOME/prepare-component-release.ps1)
Existing script that prepares ESPHome component for release:
```powershell
.\ESPHOME\prepare-component-release.ps1
```

---

## Version Definition

**Single Source of Truth**: [src/core/version.h](../src/core/version.h)

```cpp
#define EVERBLU_FW_VERSION "2.1.0"
```

All other version references are compile-time strings derived from this definition. Update this file first.

---

## Semantic Versioning Guide

| Change Type | Old → New | Example | Update |
|-------------|-----------|---------|--------|
| Bug fixes | 2.0.5 → 2.0.6 | Fixed frequency offset bug | PATCH |
| New features | 2.0.5 → 2.1.0 | Added frequency scanning | MINOR |
| Breaking changes | 2.0.0 → 3.0.0 | Changed API format | MAJOR |
| ESPHome integration | 2.0.0 → 2.1.0 | Initial ESPHome release | MINOR |

**Rule**: Always increment at least one version number. Never skip intermediate versions.

---

## Release Workflow Summary

```
┌─────────────────────────────────────────────────────────┐
│ 1. DEVELOPMENT                                          │
│    - Develop features on branches                       │
│    - Create PRs, get reviews                            │
│    - Merge to main when ready                           │
└────────────────┬────────────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────────────┐
│ 2. VALIDATION (on main branch)                          │
│    - All GitHub Actions pass ✓                          │
│    - Manual hardware testing complete ✓                 │
│    - No blocking issues                                 │
└────────────────┬────────────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────────────┐
│ 3. PREPARATION                                          │
│    - Run prepare-release.ps1/sh                         │
│    - Update CHANGELOG.md                                │
│    - Local build testing                                │
└────────────────┬────────────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────────────┐
│ 4. COMMIT & TAG                                         │
│    - git add src/core/version.h CHANGELOG.md            │
│    - git commit -m "chore: bump version to vX.Y.Z"      │
│    - git tag -a vX.Y.Z -m "Release vX.Y.Z"             │
└────────────────┬────────────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────────────┐
│ 5. PUSH                                                 │
│    - git push origin main                               │
│    - git push origin vX.Y.Z                             │
│    - GitHub auto-creates release                        │
└────────────────┬────────────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────────────┐
│ 6. RELEASE NOTES                                        │
│    - Edit GitHub Release                                │
│    - Copy CHANGELOG entries                             │
│    - Add installation instructions                      │
│    - Mark as latest release                             │
└────────────────┬────────────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────────────┐
│ 7. ANNOUNCE                                             │
│    - Notify team/users                                  │
│    - Update external docs                               │
│    - Close tracking issues                              │
└─────────────────────────────────────────────────────────┘
```

---

## Files Modified During Release

| File | Purpose | Example Change |
|------|---------|-----------------|
| [src/core/version.h](../src/core/version.h) | Firmware version | `"2.0.0"` → `"2.1.0"` |
| [CHANGELOG.md](../CHANGELOG.md) | Release notes | Add new `## [v2.1.0]` section |
| [README.md](../README.md) | Version references (optional) | Update feature descriptions |
| [ESPHOME-release/](../ESPHOME-release/) | Generated by script | Auto-generated from src/ |

---

## Key Concepts

### Version Format
- Format: `vMAJOR.MINOR.PATCH` (e.g., `v2.1.0`)
- Prefix `v` is mandatory for git tags
- Stored without `v` in code (`EVERBLU_FW_VERSION = "2.1.0"`)

### Dual-Mode Releases
- **MQTT Firmware**: Standalone PlatformIO build
- **ESPHome Component**: External component via `external_components`
- **Same Version**: Both use `EVERBLU_FW_VERSION` definition
- **Released Together**: Single tag for both modes

### CHANGELOG Rules
- ✅ Update BEFORE tagging
- ✅ Use user-focused language
- ✅ Include all breaking changes
- ✅ Never delete old entries
- ❌ Never modify after tag

### Git Tagging Rules
- ✅ Use annotated tags (`-a` flag)
- ✅ Tag message includes summary
- ✅ Tag commits are permanent
- ❌ Never delete signed tags
- ❌ Never force-push tags

### Testing Requirements
Before releasing, verify:
- [ ] PlatformIO builds all environments
- [ ] ESPHome release script runs clean
- [ ] Hardware testing (water/gas meters)
- [ ] MQTT connectivity
- [ ] ESPHome Home Assistant integration
- [ ] GitHub Actions all green

---

## Typical Release Timeline

| Phase | Time | Action |
|-------|------|--------|
| Development | Days/weeks | Feature development on branches |
| Validation | 1-2 days | Testing and QA |
| Preparation | 30 mins | Version update, changelog, local tests |
| Git ops | 5 mins | Commit, tag, push |
| Release notes | 15 mins | Edit GitHub release |
| Announce | 5 mins | Notify stakeholders |
| **Total** | **2-3 hours** | (excluding dev time) |

---

## Common Tasks

### Create a New Release
1. Read [RELEASE_PROCESS.md](RELEASE_PROCESS.md)
2. Use [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md)
3. Run `prepare-release.ps1` or `prepare-release.sh`

### Roll Back a Release
See "Rollback Procedure" in [RELEASE_PROCESS.md](RELEASE_PROCESS.md#rollback-procedure)

### Update Existing Release Notes
1. Go to [GitHub Releases](https://github.com/genestealer/everblu-meters-esp8266-improved/releases)
2. Click "Edit" on the release
3. Modify notes without re-tagging

### Check Current Version
```powershell
# Show version in code
Select-String "EVERBLU_FW_VERSION" src/core/version.h

# Show git tags
git tag -l

# Show latest release info
git describe --tags
```

### Verify Tag Was Created
```powershell
# Local
git tag -l | grep v2.1.0

# Remote
git ls-remote origin v2.1.0

# Detailed info
git show v2.1.0
```

---

## Version Numbering Examples

**Current**: v2.1.0

**Scenario**: Found a critical bug
- **Action**: Create patch release
- **New Version**: v2.1.1
- **Changelog**: Add "### Fixed" section with one bug fix

**Scenario**: Want to add frequency optimization feature
- **Action**: Create minor release
- **New Version**: v2.2.0
- **Changelog**: Add "### Added" section with feature description

**Scenario**: Completely rewrite radio driver (breaking changes)
- **Action**: Create major release
- **New Version**: v3.0.0
- **Changelog**: Add "### ⚠️ Breaking Changes" section documenting incompatibilities
- **Release Notes**: Add detailed upgrade instructions

---

## Project Versioning Context

**Project**: EverBlu Meters ESP8266/ESP32
**Repository**: https://github.com/genestealer/everblu-meters-esp8266-improved
**Release Style**: Semantic Versioning (MAJOR.MINOR.PATCH)
**Tag Format**: `vX.Y.Z` (required)
**Platforms**: 
- PlatformIO (MQTT standalone)
- ESPHome (Home Assistant integration)

**Both platforms released together with same version number**

---

## Quick Reference Commands

```powershell
# Windows PowerShell
.\prepare-release.ps1 -Version v2.2.0     # Prepare version
git add src/core/version.h CHANGELOG.md    # Stage files
git commit -m "chore: bump version to v2.2.0"
git tag -a v2.2.0 -m "Release v2.2.0"    # Create tag
git push origin main                        # Push commit
git push origin v2.2.0                      # Push tag
```

```bash
# Linux/macOS Bash
./prepare-release.sh v2.2.0                # Prepare version
git add src/core/version.h CHANGELOG.md    # Stage files
git commit -m "chore: bump version to v2.2.0"
git tag -a v2.2.0 -m "Release v2.2.0"    # Create tag
git push origin main                        # Push commit
git push origin v2.2.0                      # Push tag
```

---

## Support & Questions

For questions about:
- **Release process details**: See [RELEASE_PROCESS.md](RELEASE_PROCESS.md)
- **Step-by-step execution**: See [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md)
- **Automation scripts**: See comments in `prepare-release.ps1` or `prepare-release.sh`
- **Versioning strategy**: See semantic versioning guide above
- **Git/GitHub workflows**: See git tagging section in [RELEASE_PROCESS.md](RELEASE_PROCESS.md)

---

## Version History

Latest releases available at: https://github.com/genestealer/everblu-meters-esp8266-improved/releases

View all tags locally with:
```powershell
git tag -l --sort=-version:refname
```
