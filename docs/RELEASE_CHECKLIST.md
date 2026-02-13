# Release Execution Checklist

Quick reference for executing a release. Use this checklist during the actual release process.

**Target Release Version**: `v_._._` (fill in before starting)

---

## Phase 1: Pre-Release Validation (1-2 days before)

- [ ] All features for this release merged to `main`
- [ ] GitHub Actions all green (6 workflows pass)
  - [ ] ESP8266 build ✅
  - [ ] ESP32 build ✅
  - [ ] Code quality ✅
  - [ ] Memory & size ✅
  - [ ] Config validation ✅
  - [ ] Dependency check ✅
- [ ] Manual hardware testing complete
  - [ ] Water meter: ________________
  - [ ] Gas meter: ________________
  - [ ] ESPHome component: ________________
  - [ ] MQTT mode: ________________
- [ ] No critical open issues blocking release
- [ ] Documentation current

---

## Phase 2: Version Update (30 minutes)

1. **Update firmware version**
   ```powershell
   # Edit: src/core/version.h
   # Change EVERBLU_FW_VERSION = "X.Y.Z"
   ```
   - [ ] Old version: `_._._`
   - [ ] New version: `_._._`

2. **Update CHANGELOG**
   ```powershell
   # Edit: CHANGELOG.md
   # Add new section at TOP with today's date
   ```
   - [ ] Added section
   - [ ] Added subsections (Added, Changed, Fixed, Testing, etc.)
   - [ ] Proofread spelling and grammar
   - [ ] Verified no old entries deleted

3. **Update other version references** (verify consistency)
   ```powershell
   Select-String -Path src/core/version.h, CHANGELOG.md -Pattern "X.Y.Z"
   ```
   - [ ] All instances match new version

---

## Phase 3: Local Testing (1-2 hours)

1. **PlatformIO Build Tests**
   ```powershell
   pio run
   pio run --environment huzzah
   pio run --environment huzzah-ota
   pio run --environment nodemcu
   ```
   - [ ] All builds successful
   - [ ] No warnings (except expected external library warnings)
   - [ ] Binary sizes reasonable

2. **ESPHome Release Build**
   ```powershell
   .\ESPHOME\prepare-component-release.ps1
   ```
   - [ ] Script completed without errors
   - [ ] `ESPHOME-release/` directory created
   - [ ] File count reasonable (roughly 50-70 files)
   - [ ] `DO_NOT_EDIT.md` present in both locations

3. **Manual Hardware Test** (if available)
   - [ ] Flash to huzzah/esp8266
   - [ ] Serial monitor shows version: `X.Y.Z`
   - [ ] Water meter reads successfully
   - [ ] MQTT connects and publishes
   - [ ] ESPHome component: compile and run on test device

---

## Phase 4: Git Commit & Tag (15 minutes)

1. **Stage files**
   ```powershell
   git add src/core/version.h CHANGELOG.md
   git status  # Verify only intended files staged
   ```
   - [ ] Correct files staged
   - [ ] No accidental files included

2. **Commit**
   ```powershell
   git commit -m "chore: bump version to vX.Y.Z

   - Updated firmware version in src/core/version.h
   - Added comprehensive CHANGELOG entries
   - Verified all references are consistent"
   ```
   - [ ] Commit created successfully
   - [ ] Commit message clear

3. **Create Tag**
   ```powershell
   git tag -a vX.Y.Z -m "Release vX.Y.Z

   Major changes:
   - Feature 1
   - Feature 2
   
   See CHANGELOG.md for details"
   ```
   - [ ] Tag created locally
   - [ ] Tag message contains summary

4. **Verify Tag**
   ```powershell
   git show vX.Y.Z
   git tag -l | grep vX.Y.Z
   ```
   - [ ] Tag exists and points to correct commit
   - [ ] Commit message visible

---

## Phase 5: Push to Remote (5 minutes)

1. **Push commit**
   ```powershell
   git push origin main
   ```
   - [ ] Push successful
   - [ ] Watched GitHub Actions start

2. **Push tag**
   ```powershell
   git push origin vX.Y.Z
   ```
   - [ ] Tag pushed successfully
   - [ ] Release created on GitHub (may take 30 seconds)

3. **Verify Push**
   ```powershell
   git ls-remote origin main | head -1  # Verify commit
   git ls-remote origin vX.Y.Z          # Verify tag
   ```
   - [ ] Commit on remote
   - [ ] Tag on remote

---

## Phase 6: GitHub Release Notes (15 minutes)

1. **Navigate to Release**
   - [ ] Visit https://github.com/genestealer/everblu-meters-esp8266-improved/releases
   - [ ] Click "Edit" on the auto-generated release for `vX.Y.Z`

2. **Update Release Title**
   - [ ] Title: `Release vX.Y.Z`

3. **Add Release Notes** (copy from CHANGELOG)
   ```markdown
   # vX.Y.Z Release
   
   **Release Date**: YYYY-MM-DD
   
   ## Summary
   [One-line summary]
   
   ## What's New
   - Feature 1
   - Feature 2
   
   ## Bug Fixes
   - Fix 1
   - Fix 2
   
   ## Installation
   **MQTT**: PlatformIO build from source
   **ESPHome**: Use `external_components` with tag `vX.Y.Z`
   
   ## Known Issues
   - [From CHANGELOG]
   ```
   - [ ] Notes added
   - [ ] Checked formatting
   - [ ] Links work

4. **Final Touches**
   - [ ] Mark as **pre-release** (uncheck if normal release)
   - [ ] Set as **latest release** (check if normal release)
   - [ ] **Save** release

---

## Phase 7: Post-Release (5 minutes)

1. **Verify GitHub Actions**
   - [ ] New commit triggered CI/CD
   - [ ] All tests passing

2. **Test Release Artifact Access**
   ```powershell
   # Verify users can download release
   Invoke-WebRequest -Uri "https://github.com/psykokwak-com/everblu-meters-esp8266-improved/releases/tag/vX.Y.Z"
   ```
   - [ ] Release page accessible
   - [ ] Download links present

3. **Update Internal References** (if needed)
   - [ ] Update repository README if version mentioned
   - [ ] Notify team/stakeholders
   - [ ] Close release tracking issue (if used)

---

## Quick Command Block (Copy & Paste)

Update the version number below, then run each section:

```powershell
$VERSION = "X.Y.Z"  # ← EDIT THIS

# Verify color: git log --oneline -5
# Build test: pio run
# Release script: .\ESPHOME\prepare-component-release.ps1
# Stage: git add src/core/version.h CHANGELOG.md
# Commit: git commit -m "chore: bump version to v$VERSION"
# Tag: git tag -a v$VERSION -m "Release v$VERSION"
# Push: git push origin main
# Tag push: git push origin v$VERSION
```

---

## Rollback (If something goes wrong)

**Quick Rollback** (within minutes of tag):
```powershell
git push origin --delete vX.Y.Z       # Delete remote tag
git tag -d vX.Y.Z                     # Delete local tag
git reset --hard HEAD~1               # Undo commit
```

**Soft Rollback** (keep tag but warn users):
1. Edit GitHub release
2. Check "This is a pre-release"
3. Add warning: "❌ YANKED - Use vX.Y.Y instead"

---

## Success Criteria

✅ **Release Complete When**:
- [ ] Git tag `vX.Y.Z` exists on remote
- [ ] GitHub Release page shows correct version
- [ ] CHANGELOG.md updated with release notes
- [ ] src/core/version.h matches released version
- [ ] Building from tag produces version `X.Y.Z` output
- [ ] Users can reference `ref: vX.Y.Z` in ESPHome configs
- [ ] GitHub Actions all green for new commit

---

## Notes

- **Time estimate**: 2-3 hours total (including testing)
- **No changes after tag**: Tag is permanent. For fixes, create new patch version.
- **CHANGELOG is immutable**: Once tagged, CHANGELOG entry for that version doesn't change.
- **Dual release**: MQTT and ESPHome use same version and are released together.
