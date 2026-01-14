#!/bin/bash
# prepare-component-release.sh
# Prepares the ESPHome component for distribution by copying all necessary
# source files into a release directory structure that ESPHome can use.
# No file modifications - pure copy to preserve include paths and structure.

set -e  # Exit on error

COMPONENT_DIR="ESPHOME/components/everblu_meter"
SRC_DIR="src"
RELEASE_ROOT="ESPHOME-release"
RELEASE_DIR="$RELEASE_ROOT/everblu_meter"

echo "=========================================="
echo "EverBlu Meter Component Release Build"
echo "=========================================="
echo ""

# Check if we're in the right directory
if [ ! -d "$SRC_DIR" ] || [ ! -d "$COMPONENT_DIR" ]; then
    echo "Error: Must run from project root directory"
    echo "Expected to find:"
    echo "  - $SRC_DIR/ directory"
    echo "  - $COMPONENT_DIR/ directory"
    exit 1
fi

# Clear any existing release output to ensure a clean package
if [ -d "$RELEASE_ROOT" ]; then
    echo "Clearing existing release directory..."
    rm -rf "$RELEASE_ROOT"
fi

# Create release directory
echo "Creating release directory..."
mkdir -p "$RELEASE_DIR"

# Copy component base files
echo "Copying component files..."
cp "$COMPONENT_DIR/__init__.py" "$RELEASE_DIR/"
cp "$COMPONENT_DIR/everblu_meter.h" "$RELEASE_DIR/"
cp "$COMPONENT_DIR/everblu_meter.cpp" "$RELEASE_DIR/"
cp "$COMPONENT_DIR/README.md" "$RELEASE_DIR/" 2>/dev/null || true

# Copy entire src/ directory structure as-is (preserves includes)
echo "Copying source tree..."
cp -r "$SRC_DIR"/* "$RELEASE_DIR/"

# Remove main.cpp (standalone-only entry point, not for ESPHome)
echo "Removing standalone-only files..."
rm -f "$RELEASE_DIR/main.cpp"
rm -f "$RELEASE_DIR/adapters/implementations/define_config_provider.h"

# Flatten headers and sources into a single folder for ESPHome
echo "Flattening sources into a single folder..."
find "$RELEASE_DIR" -mindepth 2 -type f \( -name "*.h" -o -name "*.hpp" -o -name "*.c" -o -name "*.cpp" \) -exec mv -f {} "$RELEASE_DIR/" \;

# Remove MQTT publisher (not used in ESPHome)
echo "Removing MQTT publisher (ESPHome not using it)..."
find "$RELEASE_DIR" -maxdepth 1 -type f -name "mqtt_data_publisher.*" -exec rm -f {} +

# Remove now-empty directories
find "$RELEASE_DIR" -mindepth 1 -type d -empty -delete

# Rewrite include paths to reflect flattened layout (preserve esphome/*)
echo "Rewriting include paths for flattened layout..."
find "$RELEASE_DIR" -type f \( -name "*.h" -o -name "*.cpp" \) -print0 | while IFS= read -r -d '' file; do
    # Strip path prefixes like core/, services/, adapters/, src/, adapters/implementations/
    # Keep includes that start with esphome/
    sed -i.bak -E 's|#include\s+"(esphome/[^\"]+)"|#include "\1"|; t; s|#include\s+"([^"]*/)+([^"/]+)"|#include "\2"|g' "$file" && rm -f "$file.bak"
done

# Count files
TOTAL_FILES=$(find "$RELEASE_DIR" -type f | wc -l)

echo ""
echo "=========================================="
echo "✓ Release package created successfully!"
echo "=========================================="
echo ""
echo "Files copied: $TOTAL_FILES"
echo "Location: $RELEASE_DIR/"
echo ""
echo "Directory structure:"
echo "  ESPHOME-release/everblu_meter/"
echo "    __init__.py"
echo "    everblu_meter.h/.cpp"
echo "    src/ (complete source tree with original includes)"
echo ""
echo "To use with ESPHome:"
echo "  1. cp -r $RELEASE_DIR /config/esphome/custom_components/everblu_meter"
echo "  2. Use external_components: local or git in YAML"
echo ""
