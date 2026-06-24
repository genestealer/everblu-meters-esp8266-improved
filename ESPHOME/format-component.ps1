#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Format (or check) the everblu_meter ESPHome component sources with ESPHome's clang-format style.

.DESCRIPTION
    Runs clang-format over the component's own C++ sources using the .clang-format in
    ESPHOME/components/everblu_meter/ (a verbatim copy of ESPHome's upstream style:
    https://github.com/esphome/esphome/blob/dev/.clang-format).

    Only the component's own files (everblu_meter.cpp / .h) are formatted. The portable
    library under src/ keeps its own house style and is not touched.

    If clang-format is not on PATH, the latest pip package is installed automatically.
    Both this script and CI use the latest clang-format (no version to maintain); if a new
    release reformats the sources and CI fails, re-run with -Fix and commit the result.

.PARAMETER Fix
    Reformat files in place. Without this switch the script only checks formatting and
    exits non-zero if any file would change (same behaviour as CI).

.EXAMPLE
    ./ESPHOME/format-component.ps1
    Check formatting (non-destructive).

.EXAMPLE
    ./ESPHOME/format-component.ps1 -Fix
    Reformat the component sources in place.
#>
param([switch]$Fix)

$ErrorActionPreference = 'Stop'

$ComponentDir = Join-Path $PSScriptRoot 'components/everblu_meter'
$Files = Get-ChildItem -Path $ComponentDir -Include '*.cpp', '*.h' -File -Recurse |
    ForEach-Object { $_.FullName }

if (-not $Files) {
    Write-Error "No C++ sources found in $ComponentDir"
    exit 1
}

if (-not (Get-Command clang-format -ErrorAction SilentlyContinue)) {
    Write-Host 'clang-format not found; installing the latest clang-format via pip...'
    python -m pip install --quiet clang-format
    if (-not (Get-Command clang-format -ErrorAction SilentlyContinue)) {
        Write-Error "clang-format is still unavailable after pip install."
        exit 1
    }
}

if ($Fix) {
    & clang-format -style=file -i $Files
    Write-Host "Formatted $($Files.Count) file(s)."
}
else {
    & clang-format -style=file --dry-run -Werror $Files
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Formatting issues found. Fix with: ./ESPHOME/format-component.ps1 -Fix"
        exit 1
    }
    Write-Host "All component sources are correctly formatted."
}
