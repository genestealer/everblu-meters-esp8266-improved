# prepare-release.ps1
# Prepares the project for a new release on Windows
# Usage: .\prepare-release.ps1 -Version v2.2.0

param(
    [Parameter(Mandatory=$true, HelpMessage="Version to release (e.g., v2.2.0)")]
    [string]$Version
)

$ErrorActionPreference = "Stop"

# Validate version format
if ($Version -notmatch '^v\d+\.\d+\.\d+$') {
    Write-Host "Error: Invalid version format. Use: v2.2.0" -ForegroundColor Red
    exit 1
}

$VersionNumber = $Version.TrimStart('v')

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "EverBlu Meters Release Preparation" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Target version: $Version" -ForegroundColor Green
Write-Host ""

# Step 1: Verify we're in the right directory
if (-not (Test-Path "platformio.ini") -or -not (Test-Path "CHANGELOG.md")) {
    Write-Host "Error: Must run from project root" -ForegroundColor Red
    Write-Host "Expected: platformio.ini and CHANGELOG.md"
    exit 1
}

# Step 2: Check git status
Write-Host "Checking git status..." -ForegroundColor Yellow
$gitStatus = & git status --porcelain
if ($gitStatus) {
    Write-Host "⚠️  Working directory has uncommitted changes" -ForegroundColor Yellow
    Write-Host "Please commit or stash changes before releasing"
    & git status --short
    exit 1
}
Write-Host "✓ Working directory clean" -ForegroundColor Green
Write-Host ""

# Step 3: Get current version
Write-Host "Reading current version..." -ForegroundColor Yellow
$versionContent = Get-Content "src\core\version.h" -Raw
$currentVersionMatch = [regex]::Match($versionContent, 'EVERBLU_FW_VERSION "(\d+\.\d+\.\d+)"')
if (-not $currentVersionMatch.Success) {
    Write-Host "Error: Could not find version in src/core/version.h" -ForegroundColor Red
    exit 1
}
$currentVersion = $currentVersionMatch.Groups[1].Value
Write-Host "Current version: $currentVersion" -ForegroundColor Yellow
Write-Host "New version: $VersionNumber" -ForegroundColor Yellow
Write-Host ""

# Step 4: Update version.h
Write-Host "Updating src\core\version.h..." -ForegroundColor Yellow
$newContent = $versionContent -replace `
    "EVERBLU_FW_VERSION `"$currentVersion`"", `
    "EVERBLU_FW_VERSION `"$VersionNumber`""

Set-Content -Path "src\core\version.h" -Value $newContent -NoNewline
echo ""
Write-Host "✓ Updated version.h" -ForegroundColor Green
Write-Host ""

# Step 5: Show diff
Write-Host "Changes to src\core\version.h:" -ForegroundColor Yellow
& git diff src\core\version.h
Write-Host ""

# Step 6: Verify the change
$updatedContent = Get-Content "src\core\version.h" -Raw
$updatedVersionMatch = [regex]::Match($updatedContent, 'EVERBLU_FW_VERSION "(\d+\.\d+\.\d+)"')
if (-not $updatedVersionMatch.Success) {
    Write-Host "❌ Version not updated correctly" -ForegroundColor Red
    exit 1
}
$updatedVersion = $updatedVersionMatch.Groups[1].Value
if ($updatedVersion -ne $VersionNumber) {
    Write-Host "❌ Updated version ($updatedVersion) doesn't match target ($VersionNumber)" -ForegroundColor Red
    exit 1
}
Write-Host "✓ Version updated successfully" -ForegroundColor Green
Write-Host ""

# Step 7: Run ESPHome release build
Write-Host "Running ESPHome release build..." -ForegroundColor Yellow
if (Test-Path "ESPHOME\prepare-component-release.ps1") {
    & ".\ESPHOME\prepare-component-release.ps1"
    Write-Host "✓ ESPHome release built" -ForegroundColor Green
}
else {
    Write-Host "⚠️  ESPHome release script not found (skipping)" -ForegroundColor Yellow
}
Write-Host ""

# Step 8: Show next steps
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "✓ Release preparation complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next steps:" -ForegroundColor White
Write-Host ""
Write-Host "1. Review CHANGELOG.md and add entry for version $Version" -ForegroundColor White
Write-Host "   Format:" -ForegroundColor White
Write-Host "   ## [$Version] - YYYY-MM-DD" -ForegroundColor Gray
Write-Host "   ### Added" -ForegroundColor Gray
Write-Host "   ### Changed" -ForegroundColor Gray
Write-Host "   ### Fixed" -ForegroundColor Gray
Write-Host ""
Write-Host "2. Commit changes:" -ForegroundColor White
Write-Host "   git add src\core\version.h CHANGELOG.md" -ForegroundColor Gray
Write-Host "   git commit -m ""chore: bump version to $Version""" -ForegroundColor Gray
Write-Host ""
Write-Host "3. Create git tag:" -ForegroundColor White
Write-Host "   git tag -a $Version -m ""Release $Version""" -ForegroundColor Gray
Write-Host ""
Write-Host "4. Push to remote:" -ForegroundColor White
Write-Host "   git push origin main" -ForegroundColor Gray
Write-Host "   git push origin $Version" -ForegroundColor Gray
Write-Host ""
Write-Host "5. Create GitHub Release with CHANGELOG entries" -ForegroundColor White
Write-Host ""
