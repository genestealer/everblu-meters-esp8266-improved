#!/bin/bash
# prepare-component-release.sh
# Prepares the ESPHome component for standalone distribution by copying
# all necessary source files into the component directory

set -e  # Exit on error

COMPONENT_DIR="ESPHOME/components/everblu_meter"
SRC_DIR="src"

echo "=========================================="
echo "EverBlu Meter Component Release Preparation"
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

# Create backup if src already exists in component
if [ -d "$COMPONENT_DIR/src" ]; then
    echo "Warning: $COMPONENT_DIR/src already exists"
    read -p "Create backup and continue? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborted."
        exit 1
    fi
    BACKUP_DIR="$COMPONENT_DIR/src.backup.$(date +%Y%m%d_%H%M%S)"
    echo "Creating backup: $BACKUP_DIR"
    mv "$COMPONENT_DIR/src" "$BACKUP_DIR"
fi

# Create source directories in component
echo "Creating directory structure..."
mkdir -p "$COMPONENT_DIR/src/core"
mkdir -p "$COMPONENT_DIR/src/services"
mkdir -p "$COMPONENT_DIR/src/adapters"
mkdir -p "$COMPONENT_DIR/src/adapters/implementations"

# Copy core files
echo "Copying core files..."
cp "$SRC_DIR/core/cc1101.h" "$COMPONENT_DIR/src/core/"
cp "$SRC_DIR/core/cc1101.cpp" "$COMPONENT_DIR/src/core/"
cp "$SRC_DIR/core/utils.h" "$COMPONENT_DIR/src/core/"
cp "$SRC_DIR/core/utils.cpp" "$COMPONENT_DIR/src/core/"
cp "$SRC_DIR/core/version.h" "$COMPONENT_DIR/src/core/"
echo "  ✓ cc1101.h, cc1101.cpp"
echo "  ✓ utils.h, utils.cpp"
echo "  ✓ version.h"

# Copy service files
echo "Copying service files..."
cp "$SRC_DIR/services/meter_reader.h" "$COMPONENT_DIR/src/services/"
cp "$SRC_DIR/services/meter_reader.cpp" "$COMPONENT_DIR/src/services/"
cp "$SRC_DIR/services/frequency_manager.h" "$COMPONENT_DIR/src/services/"
cp "$SRC_DIR/services/frequency_manager.cpp" "$COMPONENT_DIR/src/services/"
cp "$SRC_DIR/services/schedule_manager.h" "$COMPONENT_DIR/src/services/"
cp "$SRC_DIR/services/schedule_manager.cpp" "$COMPONENT_DIR/src/services/"
cp "$SRC_DIR/services/meter_history.h" "$COMPONENT_DIR/src/services/"
cp "$SRC_DIR/services/meter_history.cpp" "$COMPONENT_DIR/src/services/"
cp "$SRC_DIR/services/storage_abstraction.h" "$COMPONENT_DIR/src/services/"
cp "$SRC_DIR/services/storage_abstraction.cpp" "$COMPONENT_DIR/src/services/"
echo "  ✓ meter_reader.h, meter_reader.cpp"
echo "  ✓ frequency_manager.h, frequency_manager.cpp"
echo "  ✓ schedule_manager.h, schedule_manager.cpp"
echo "  ✓ meter_history.h, meter_history.cpp"
echo "  ✓ storage_abstraction.h, storage_abstraction.cpp"

# Copy adapter interfaces
echo "Copying adapter interfaces..."
cp "$SRC_DIR/adapters/config_provider.h" "$COMPONENT_DIR/src/adapters/"
cp "$SRC_DIR/adapters/time_provider.h" "$COMPONENT_DIR/src/adapters/"
cp "$SRC_DIR/adapters/data_publisher.h" "$COMPONENT_DIR/src/adapters/"
echo "  ✓ config_provider.h"
echo "  ✓ time_provider.h"
echo "  ✓ data_publisher.h"

# Copy ESPHome adapter implementations
echo "Copying ESPHome adapters..."
cp "$SRC_DIR/adapters/implementations/esphome_config_provider.h" "$COMPONENT_DIR/src/adapters/implementations/"
cp "$SRC_DIR/adapters/implementations/esphome_config_provider.cpp" "$COMPONENT_DIR/src/adapters/implementations/"
cp "$SRC_DIR/adapters/implementations/esphome_time_provider.h" "$COMPONENT_DIR/src/adapters/implementations/"
cp "$SRC_DIR/adapters/implementations/esphome_time_provider.cpp" "$COMPONENT_DIR/src/adapters/implementations/"
cp "$SRC_DIR/adapters/implementations/esphome_data_publisher.h" "$COMPONENT_DIR/src/adapters/implementations/"
cp "$SRC_DIR/adapters/implementations/esphome_data_publisher.cpp" "$COMPONENT_DIR/src/adapters/implementations/"
echo "  ✓ esphome_config_provider.h, esphome_config_provider.cpp"
echo "  ✓ esphome_time_provider.h, esphome_time_provider.cpp"
echo "  ✓ esphome_data_publisher.h, esphome_data_publisher.cpp"

# Copy meter definitions
echo "Copying meter definitions..."
cp "$SRC_DIR/everblu_meters.h" "$COMPONENT_DIR/src/"
echo "  ✓ everblu_meters.h"

# Count files
TOTAL_FILES=$(find "$COMPONENT_DIR/src" -type f | wc -l)

echo ""
echo "=========================================="
echo "✓ Component prepared successfully!"
echo "=========================================="
echo ""
echo "Files copied: $TOTAL_FILES"
echo "Target directory: $COMPONENT_DIR/src/"
echo ""
echo "Next steps:"
echo ""
echo "1. Update include paths in everblu_meter.h:"
echo "   Change from: #include \"../../src/services/meter_reader.h\""
echo "   Change to:   #include \"src/services/meter_reader.h\""
echo ""
echo "2. Test compilation:"
echo "   esphome compile your-test-config.yaml"
echo ""
echo "3. Package for distribution:"
echo "   cd ESPHOME/components"
echo "   tar czf everblu_meter.tar.gz everblu_meter/"
echo "   or"
echo "   zip -r everblu_meter.zip everblu_meter/"
echo ""
