<#
.SYNOPSIS
    Capture the MQTT (PlatformIO / huzzah) serial monitor to a timestamped log file.

.DESCRIPTION
    Runs the PlatformIO serial monitor with the `time` filter (per-line host
    timestamps) and tees the output to a timestamped file so the timing is
    accurate and reproducible. Optionally flashes the firmware first with -Upload.

    Output is written to the -OutDir folder (default: temp\) as:
        mqtt_<yyyy-MM-dd_HH-mm-ss>.log

.EXAMPLE
    ./scripts/capture-mqtt-log.ps1
    # Monitor only, save to temp\mqtt_<timestamp>.log

.EXAMPLE
    ./scripts/capture-mqtt-log.ps1 -Upload
    # Flash the huzzah build first, then monitor + log

.NOTES
    Stop the capture with Ctrl+C. Only one process can own the COM port, so close
    any other open serial monitor first (otherwise: "could not open port ...
    Access is denied").
#>
[CmdletBinding()]
param(
    [switch]$Upload,
    [string]$Environment = "huzzah",
    [string]$OutDir = "temp"
)

$ErrorActionPreference = "Stop"

# Resolve repo root (parent of this script's folder) and the pio executable.
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$pio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"
if (-not (Test-Path $pio)) {
    $pioCmd = Get-Command platformio -ErrorAction SilentlyContinue
    if ($pioCmd) { $pio = $pioCmd.Source }
    else { throw "platformio.exe not found. Edit the script's `$pio path." }
}

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}

$timestamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$logPath = Join-Path $OutDir "mqtt_$timestamp.log"

if ($Upload) {
    Write-Host "Flashing '$Environment' firmware..." -ForegroundColor Cyan
    & $pio run -e $Environment -t upload
}

Write-Host "Capturing serial monitor to $logPath (Ctrl+C to stop)..." -ForegroundColor Cyan
& $pio device monitor -e $Environment -f time 2>&1 | Tee-Object -FilePath $logPath
