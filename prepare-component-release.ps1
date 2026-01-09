# prepare-component-release.ps1
# Prepares the ESPHome component for distribution by copying all necessary
# source files into a release directory structure that ESPHome can use.
# No file modifications - pure copy to preserve include paths and structure.

$ErrorActionPreference = "Stop"

$COMPONENT_DIR = "ESPHOME\components\everblu_meter"
$SRC_DIR = "src"
$RELEASE_ROOT = "ESPHOME-release"
$RELEASE_DIR = "$RELEASE_ROOT\everblu_meter"

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "EverBlu Meter Component Release Build" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""

# Check if we're in the right directory
if (!(Test-Path $SRC_DIR) -or !(Test-Path $COMPONENT_DIR)) {
    Write-Host "Error: Must run from project root directory" -ForegroundColor Red
    Write-Host "Expected to find:"
    Write-Host "  - $SRC_DIR\ directory"
    Write-Host "  - $COMPONENT_DIR\ directory"
    exit 1
}

# Clear any existing release output to ensure a clean package
if (Test-Path $RELEASE_ROOT) {
    Write-Host "Clearing existing release directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $RELEASE_ROOT
}

# Create release directory
Write-Host "Creating release directory..." -ForegroundColor Green
New-Item -ItemType Directory -Force -Path "$RELEASE_DIR" | Out-Null

# Copy component base files
Write-Host "Copying component files..." -ForegroundColor Green
Copy-Item "$COMPONENT_DIR\__init__.py" "$RELEASE_DIR\"
Copy-Item "$COMPONENT_DIR\everblu_meter.h" "$RELEASE_DIR\"
Copy-Item "$COMPONENT_DIR\everblu_meter.cpp" "$RELEASE_DIR\"
Copy-Item "$COMPONENT_DIR\README.md" "$RELEASE_DIR\" -ErrorAction SilentlyContinue

# Copy entire src/ directory structure as-is (preserves includes)
Write-Host "Copying source tree..." -ForegroundColor Green
Copy-Item -Recurse "$SRC_DIR\*" "$RELEASE_DIR\"

# Remove main.cpp (standalone-only entry point, not for ESPHome)
Write-Host "Removing standalone-only files..." -ForegroundColor Yellow
Remove-Item "$RELEASE_DIR\main.cpp" -ErrorAction SilentlyContinue
Remove-Item "$RELEASE_DIR\adapters\implementations\define_config_provider.h" -ErrorAction SilentlyContinue

# Flatten headers and sources into a single folder for ESPHome
Write-Host "Flattening sources into a single folder..." -ForegroundColor Green
$filesToMove = Get-ChildItem -Path $RELEASE_DIR -Recurse -Include *.h,*.hpp,*.c,*.cpp | Where-Object { $_.DirectoryName -ne (Resolve-Path $RELEASE_DIR) }
foreach ($f in $filesToMove) {
    $dest = Join-Path $RELEASE_DIR $f.Name
    Move-Item -Force $f.FullName $dest
}

# Remove MQTT publisher files (not used in ESPHome)
Write-Host "Removing MQTT publisher (ESPHome not using it)..." -ForegroundColor Yellow
Get-ChildItem -Path $RELEASE_DIR -Filter "mqtt_data_publisher.*" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

# Remove now-empty subdirectories
Get-ChildItem -Path $RELEASE_DIR -Directory -Recurse | Sort-Object FullName -Descending | ForEach-Object {
    if (-not (Get-ChildItem -Path $_.FullName -Recurse -File)) { Remove-Item $_.FullName -Force }
}

# Rewrite include paths to reflect flattened layout (preserve esphome/* and system includes)
Write-Host "Rewriting include paths for flattened layout..." -ForegroundColor Green
$allFiles = Get-ChildItem -Path $RELEASE_DIR -Filter *.h | Select-Object -ExpandProperty FullName
$allFiles += Get-ChildItem -Path $RELEASE_DIR -Filter *.cpp | Select-Object -ExpandProperty FullName
foreach ($file in $allFiles) {
    $content = Get-Content $file -Raw
    # Strip any leading path segments like core/, services/, adapters/, src/, adapters/implementations/
    $content = $content -replace '#include\s+"(?!esphome/)(?:[^"/]+/)+([^"/]+)"', '#include "$1"'
    Set-Content -Path $file -Value $content -NoNewline
}

Write-Host ""
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Release package created successfully!" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Location: $RELEASE_DIR\" -ForegroundColor Green
Write-Host ""
Write-Host "Directory structure:" -ForegroundColor Green
Write-Host "  ESPHOME-release/everblu_meter/" -ForegroundColor White
Write-Host "  ├── __init__.py" -ForegroundColor White
Write-Host "  ├── everblu_meter.h/.cpp" -ForegroundColor White
Write-Host "  └── src/ (complete source tree with original includes)" -ForegroundColor White
Write-Host ""
Write-Host "To use with ESPHome:" -ForegroundColor Green
Write-Host "  1. Copy to ESPHome: Copy-Item -Recurse '$RELEASE_DIR' '/config/esphome/custom_components/everblu_meter'" -ForegroundColor Cyan
Write-Host "  2. In YAML use external_components: local path or git" -ForegroundColor Cyan
Write-Host ""
