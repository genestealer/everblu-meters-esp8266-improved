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
#
# First time: pip install -r scripts/requirements-dev.txt
# Steps whose tooling is missing are reported as SKIPPED rather than failing,
# so a partial environment still gets useful results.

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

    $code = 0
    $previous = $ErrorActionPreference

    # Several of these tools write progress to stderr even on success (gcovr
    # logs "(INFO) Reading coverage data..." there). Under a Stop preference
    # PowerShell turns that into a terminating NativeCommandError, so the step
    # would be reported as failed despite a zero exit code. Judge success by the
    # exit code alone.
    $ErrorActionPreference = "Continue"
    try {
        $global:LASTEXITCODE = 0
        & $Action
        $code = $LASTEXITCODE
    }
    catch {
        Write-Host "    $($_.Exception.Message)" -ForegroundColor Red
        $code = 1
    }
    finally {
        $ErrorActionPreference = $previous
    }

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

function Skip-Step {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Reason
    )

    Write-Host ""
    Write-Host "==> $Name" -ForegroundColor Cyan
    $script:results[$Name] = "SKIP"
    Write-Host "    $Name : SKIPPED - $Reason" -ForegroundColor Yellow
}

function Test-PythonModule {
    param([Parameter(Mandatory)][string]$Module)

    # Probe the interpreter the script will actually invoke, which is not
    # necessarily the one that installed the package. All streams are discarded:
    # a failed import writes a traceback to stderr, which PowerShell would
    # otherwise turn into a terminating NativeCommandError.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & python -c "import $Module" *> $null
        return ($LASTEXITCODE -eq 0)
    }
    catch {
        return $false
    }
    finally {
        $ErrorActionPreference = $previous
    }
}

$installHint = "install the dev dependencies: pip install -r scripts/requirements-dev.txt"

# Run from the repository root regardless of where the script was invoked.
Push-Location (Join-Path $PSScriptRoot "..")

$hadSerial = Test-Path Env:\EVERBLU_NATIVE_SERIAL
if ($Serial) {
    $env:EVERBLU_NATIVE_SERIAL = "1"
}

try {
    if (-not (Get-Command pio -ErrorAction SilentlyContinue)) {
        Write-Host "PlatformIO ('pio') is not on PATH. Please $installHint" -ForegroundColor Red
        exit 1
    }

    # Host test suites. -e native covers the MQTT-mode code; -e native_esphome
    # rebuilds the shared sources with USE_ESPHOME so the ESPHome publisher,
    # which is otherwise compiled out entirely, is exercised too.
    Invoke-Step "Host tests (native)" { pio test -e native }
    Invoke-Step "Host tests (native_esphome)" { pio test -e native_esphome }

    # ESPHome YAML schema validation. These import the real ESPHome validators,
    # so they need both esphome and pytest.
    if (-not (Test-PythonModule "pytest")) {
        Skip-Step "ESPHome config tests" "pytest is not available to '$((Get-Command python).Source)'. Please $installHint"
    }
    elseif (-not (Test-PythonModule "esphome")) {
        Skip-Step "ESPHome config tests" "esphome is not available to '$((Get-Command python).Source)'. Please $installHint"
    }
    else {
        Invoke-Step "ESPHome config tests" { python -m pytest tests/esphome -q }
    }

    if ($Build) {
        Invoke-Step "Firmware build (huzzah)" { pio run -e huzzah }
        Invoke-Step "Firmware build (esp32dev)" { pio run -e esp32dev }
    }

    if ($Coverage) {
        $gcovrArgs = @(
            "--root", ".",
            "--object-directory", ".pio/build/",
            "--print-summary",
            "--filter", "src/core/crc_kermit.cpp",
            "--filter", "src/core/radian_parser.cpp",
            "--filter", "src/core/radian_decoder.cpp",
            "--filter", "src/core/utils.cpp",
            "--filter", "src/adapters/implementations/esphome_data_publisher.cpp",
            "--filter", "src/services/frequency_manager.cpp",
            "--filter", "src/services/meter_history.cpp",
            "--filter", "src/services/meter_reader.cpp",
            "--filter", "src/services/schedule_manager.cpp"
        )

        # Prefer the console script, but fall back to the module: a virtual
        # environment's Scripts directory is not always on PATH in the shell
        # that installed into it.
        if (Get-Command gcovr -ErrorAction SilentlyContinue) {
            Invoke-Step "Coverage" { gcovr @gcovrArgs }
        }
        elseif (Test-PythonModule "gcovr") {
            Invoke-Step "Coverage" { python -m gcovr @gcovrArgs }
        }
        else {
            Skip-Step "Coverage" "gcovr is not available. Please $installHint"
        }
    }

    Write-Host ""
    Write-Host "================ SUMMARY ================" -ForegroundColor Cyan
    foreach ($entry in $results.GetEnumerator()) {
        $colour = switch ($entry.Value) {
            "PASS" { "Green" }
            "SKIP" { "Yellow" }
            default { "Red" }
        }
        Write-Host ("  {0,-32} {1}" -f $entry.Key, $entry.Value) -ForegroundColor $colour
    }
    Write-Host "=========================================" -ForegroundColor Cyan

    if ($failed.Count -gt 0) {
        Write-Host ""
        Write-Host "$($failed.Count) step(s) failed: $($failed -join ', ')" -ForegroundColor Red
        exit 1
    }

    $skipped = @($results.GetEnumerator() | Where-Object { $_.Value -eq "SKIP" })

    Write-Host ""
    Write-Host "All checks passed." -ForegroundColor Green
    if ($skipped.Count -gt 0) {
        Write-Host "$($skipped.Count) step(s) skipped for missing tools. To $installHint" -ForegroundColor Yellow
    }
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
