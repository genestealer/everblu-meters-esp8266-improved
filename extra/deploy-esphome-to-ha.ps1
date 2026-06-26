<#
.SYNOPSIS
    Build the ESPHome component release and deploy it to the Home Assistant instance.

.DESCRIPTION
    1. Runs prepare-component-release.ps1 to regenerate ESPHOME-release\everblu_meter.
    2. Mirrors the generated component to the HA external_components share.

    Uses robocopy /MIR instead of Remove-Item + Copy-Item because the prepared
    release files are marked read-only, which caused the old command to abort
    partway and leave a partially-copied (broken) component on the share.

.EXAMPLE
    .\extra\deploy-esphome-to-ha.ps1
#>

[CmdletBinding()]
param(
    [string]$Destination = '\\192.168.10.21\config\esphome\external_components\everblu_meter'
)

$ErrorActionPreference = 'Stop'

# Resolve repo root (parent of this script's folder) and work from there.
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

# 1. Regenerate the release package.
& .\ESPHOME\prepare-component-release.ps1

$source = (Resolve-Path '.\ESPHOME-release\everblu_meter').Path

# 2. Clear read-only attributes on any existing deployed files so they can be replaced.
if (Test-Path $Destination) {
    Get-ChildItem $Destination -Recurse -File -Force | ForEach-Object { $_.IsReadOnly = $false }
}

# 3. Mirror the component to the share (adds new, overwrites changed, removes stale).
robocopy $source $Destination /MIR /R:2 /W:1 | Out-Null

# robocopy exit codes < 8 indicate success (0-7). 8+ means at least one failure.
if ($LASTEXITCODE -ge 8) {
    throw "robocopy failed with exit code $LASTEXITCODE"
}

$srcCount = (Get-ChildItem $source -File).Count
$dstCount = (Get-ChildItem $Destination -File).Count
Write-Host "Deploy complete: $dstCount/$srcCount files mirrored to $Destination" -ForegroundColor Green
