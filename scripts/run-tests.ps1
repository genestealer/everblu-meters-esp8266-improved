# run-tests.ps1
# Runs the same checks CI runs, in the same order, without any hardware.
#
#   ./scripts/run-tests.ps1              # host test suites only (fast)
#   ./scripts/run-tests.ps1 -Build       # also compile both firmware targets
#   ./scripts/run-tests.ps1 -Coverage    # also produce a gcovr summary
#   ./scripts/run-tests.ps1 -All         # everything
#   ./scripts/run-tests.ps1 -Serial      # mirror firmware log output to stdout
#
# Nothing here needs a board, a radio or a meter.

[CmdletBinding()]
param(
    # Compile the ESP8266 and ESP32 firmware. Host tests cannot catch a
    # board-only compile break, so run this before pushing.
    [switch]$Build,

    # Collect coverage over the host-tested sources.
    [switch]$Coverage,

    # Equivalent to -Build -Coverage.
    [switch]$All,

    # Show the firmware's own log output, which the suites suppress by default.
    [switch]$Serial
)

$ErrorActionPreference = "Stop"

if ($All) {
    $Build = $true
    $Coverage = $true
}

$results = [ordered]@{}
$failed = @()

function Invoke-Step {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][scriptblock]$Action
    )

    Write-Host ""
    Write-Host "==> $Name" -ForegroundColor Cyan

    & $Action
    $code = $LASTEXITCODE

    if ($code -eq 0) {
        $script:results[$Name] = "PASS"
        Write-Host "    $Name : PASS" -ForegroundColor Green
    }
    else {
        $script:results[$Name] = "FAIL"
        $script:failed += $Name
        Write-Host "    $Name : FAIL (exit $code)" -ForegroundColor Red
    }
}

# Run from the repository root regardless of where the script was invoked.
Push-Location (Join-Path $PSScriptRoot "..")

$hadSerial = Test-Path Env:\EVERBLU_NATIVE_SERIAL
if ($Serial) {
    $env:EVERBLU_NATIVE_SERIAL = "1"
}

try {
    # Host test suites. -e native covers the MQTT-mode code; -e native_esphome
    # rebuilds the shared sources with USE_ESPHOME so the ESPHome publisher,
    # which is otherwise compiled out entirely, is exercised too.
    Invoke-Step "Host tests (native)" { pio test -e native }
    Invoke-Step "Host tests (native_esphome)" { pio test -e native_esphome }

    # ESPHome YAML schema validation.
    Invoke-Step "ESPHome config tests" { python -m pytest tests/esphome -q }

    if ($Build) {
        Invoke-Step "Firmware build (huzzah)" { pio run -e huzzah }
        Invoke-Step "Firmware build (esp32dev)" { pio run -e esp32dev }
    }

    if ($Coverage) {
        Invoke-Step "Coverage" {
            gcovr --root . --object-directory .pio/build/ --print-summary `
                --filter src/core/crc_kermit.cpp `
                --filter src/core/radian_parser.cpp `
                --filter src/core/radian_decoder.cpp `
                --filter src/core/utils.cpp `
                --filter src/adapters/implementations/esphome_data_publisher.cpp `
                --filter src/services/frequency_manager.cpp `
                --filter src/services/meter_history.cpp `
                --filter src/services/meter_reader.cpp `
                --filter src/services/schedule_manager.cpp
        }
    }

    Write-Host ""
    Write-Host "================ SUMMARY ================" -ForegroundColor Cyan
    foreach ($entry in $results.GetEnumerator()) {
        $colour = if ($entry.Value -eq "PASS") { "Green" } else { "Red" }
        Write-Host ("  {0,-32} {1}" -f $entry.Key, $entry.Value) -ForegroundColor $colour
    }
    Write-Host "=========================================" -ForegroundColor Cyan

    if ($failed.Count -gt 0) {
        Write-Host ""
        Write-Host "$($failed.Count) step(s) failed: $($failed -join ', ')" -ForegroundColor Red
        exit 1
    }

    Write-Host ""
    Write-Host "All checks passed." -ForegroundColor Green
    if (-not $Build) {
        Write-Host "Firmware was not compiled. Run with -Build before pushing." -ForegroundColor Yellow
    }
    exit 0
}
finally {
    if ($Serial -and -not $hadSerial) {
        Remove-Item Env:\EVERBLU_NATIVE_SERIAL -ErrorAction SilentlyContinue
    }
    Pop-Location
}
