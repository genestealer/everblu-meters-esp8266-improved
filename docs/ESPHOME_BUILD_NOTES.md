# ESPHome Build Configuration Notes

## Source File Access

The ESPHome component needs access to the source files in `../../src/`. There are several ways to handle this:

### Option 1: Copy Source Files (Recommended for Distribution)

Copy the necessary source files into the component directory:

```bash
cd ESPHOME/components/everblu_meter/

# Create src directories
mkdir -p src/core src/services src/adapters/implementations

# Copy core files
cp ../../../src/core/cc1101.h src/core/
cp ../../../src/core/cc1101.cpp src/core/
cp ../../../src/core/utils.h src/core/
cp ../../../src/core/utils.cpp src/core/

# Copy service files
cp ../../../src/services/meter_reader.h src/services/
cp ../../../src/services/meter_reader.cpp src/services/
cp ../../../src/services/frequency_manager.h src/services/
cp ../../../src/services/frequency_manager.cpp src/services/
cp ../../../src/services/schedule_manager.h src/services/
cp ../../../src/services/schedule_manager.cpp src/services/
# ... etc

# Copy adapter interfaces
cp ../../../src/adapters/*.h src/adapters/

# Copy ESPHome adapter implementations
cp ../../../src/adapters/implementations/esphome_*.h src/adapters/implementations/
cp ../../../src/adapters/implementations/esphome_*.cpp src/adapters/implementations/
```

Then update includes in `everblu_meter.h`:

```cpp
#include "src/services/meter_reader.h"
#include "src/adapters/implementations/esphome_config_provider.h"
#include "src/adapters/implementations/esphome_time_provider.h"
#include "src/adapters/implementations/esphome_data_publisher.h"
```

### Option 2: Symbolic Links (Development)

For development, use symbolic links:

```bash
cd ESPHOME/components/everblu_meter/
ln -s ../../../src src
```

### Option 3: External Components with Full Repo

Use ESPHome's `external_components` with the full repository:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/yourusername/everblu-meters-esp8266-improved.git
      ref: main
    components: [ everblu_meter ]
    refresh: 1d
```

Then ESPHome will clone the entire repository and have access to `../../src/`.

### Option 4: Modify Build Configuration

Create a custom build script in the component that tells ESPHome where to find sources.

In `__init__.py`, add:

```python
# Add source directory to include paths
cg.add_build_flag("-I../../src")
cg.add_build_flag("-I../../src/core")
cg.add_build_flag("-I../../src/services")
cg.add_build_flag("-I../../src/adapters")
cg.add_build_flag("-I../../src/adapters/implementations")

# Add source files to build
cg.add_library("everblu_src", "../../src")
```

## Recommended Approach

For **distribution** and **ease of use**: **Option 1** (copy files)

For **development**: **Option 2** (symbolic links) or **Option 3** (git external_components)

## Current Implementation

The current implementation uses **relative paths** (`../../src/`), which works with **Option 3** (external_components pointing to the git repo).

## Build Script

Here's a script to prepare the component for standalone distribution:

```bash
#!/bin/bash
# prepare-component-release.sh

COMPONENT_DIR="ESPHOME/components/everblu_meter"
SRC_DIR="src"

echo "Preparing EverBlu Meter component for release..."

# Create source directories in component
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

# Copy adapter interfaces
echo "Copying adapter interfaces..."
cp "$SRC_DIR/adapters/config_provider.h" "$COMPONENT_DIR/src/adapters/"
cp "$SRC_DIR/adapters/time_provider.h" "$COMPONENT_DIR/src/adapters/"
cp "$SRC_DIR/adapters/data_publisher.h" "$COMPONENT_DIR/src/adapters/"

# Copy ESPHome adapter implementations
echo "Copying ESPHome adapters..."
cp "$SRC_DIR/adapters/implementations/esphome_config_provider.h" "$COMPONENT_DIR/src/adapters/implementations/"
cp "$SRC_DIR/adapters/implementations/esphome_config_provider.cpp" "$COMPONENT_DIR/src/adapters/implementations/"
cp "$SRC_DIR/adapters/implementations/esphome_time_provider.h" "$COMPONENT_DIR/src/adapters/implementations/"
cp "$SRC_DIR/adapters/implementations/esphome_time_provider.cpp" "$COMPONENT_DIR/src/adapters/implementations/"
cp "$SRC_DIR/adapters/implementations/esphome_data_publisher.h" "$COMPONENT_DIR/src/adapters/implementations/"
cp "$SRC_DIR/adapters/implementations/esphome_data_publisher.cpp" "$COMPONENT_DIR/src/adapters/implementations/"

# Copy meter definitions
echo "Copying meter definitions..."
cp "$SRC_DIR/everblu_meters.h" "$COMPONENT_DIR/src/"

echo "Component prepared! Files copied to $COMPONENT_DIR/src/"
echo ""
echo "Now update includes in everblu_meter.h from:"
echo '  #include "../../src/services/meter_reader.h"'
echo "to:"
echo '  #include "src/services/meter_reader.h"'
```

## ESPHome Build Process

When ESPHome builds a component:

1. Reads `__init__.py` and processes configuration
2. Generates C++ code based on configuration
3. Compiles all `.cpp` files found in component directory
4. Links everything together

The component directory structure should be:

```
everblu_meter/
├── __init__.py              # Configuration schema
├── everblu_meter.h          # Component header
├── everblu_meter.cpp        # Component implementation
└── src/                     # Source files (if using Option 1)
    ├── core/
    ├── services/
    └── adapters/
```

## Testing Build Configuration

Test compilation with:

```bash
# Validate configuration
esphome config test-config.yaml

# Compile (doesn't upload)
esphome compile test-config.yaml

# Check for errors
echo $?
```

## Common Build Errors

### "Cannot find meter_reader.h"

**Solution**: Source files not accessible. Use Option 1 (copy) or Option 3 (git external_components).

### "Multiple definition of CC1101::init"

**Solution**: Ensure source files are only included once. Check for duplicate `#include` statements.

### "SPI not defined"

**Solution**: Add SPI component to YAML:

```yaml
spi:
  clk_pin: GPIO14
  miso_pin: GPIO12
  mosi_pin: GPIO13
```

### "USE_ESPHOME not defined"

**Solution**: ESPHome should define this automatically. If not, add to `__init__.py`:

```python
cg.add_define("USE_ESPHOME")
```

## Verification

After build configuration is set up, verify:

1. Configuration validates: `esphome config your-config.yaml`
2. Compilation succeeds: `esphome compile your-config.yaml`
3. No warnings about missing files
4. Binary size is reasonable (<1MB for ESP8266)
5. Upload and test on actual hardware

## Current Status

The current implementation assumes **Option 3** (external_components with git URL). For standalone distribution, run the preparation script to create **Option 1** structure.
