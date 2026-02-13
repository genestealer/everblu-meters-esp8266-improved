#!/bin/bash
# prepare-release.sh
# Prepares the project for a new release on Linux/macOS
# Usage: ./prepare-release.sh v2.2.0

set -e

if [ $# -lt 1 ]; then
    echo "Usage: $0 <version>"
    echo "Example: $0 v2.2.0"
    exit 1
fi

NEW_VERSION="$1"
VERSION_NUMBER="${NEW_VERSION#v}"  # Remove 'v' prefix

BLUE='\033[0;34m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'  # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}EverBlu Meters Release Preparation${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo -e "Target version: ${GREEN}${NEW_VERSION}${NC}"
echo ""

# Step 1: Verify we're in the right directory
if [ ! -f "platformio.ini" ] || [ ! -f "CHANGELOG.md" ]; then
    echo -e "${YELLOW}Error: Must run from project root${NC}"
    echo "Expected: platformio.ini and CHANGELOG.md"
    exit 1
fi

# Step 2: Check git status
echo -e "${YELLOW}Checking git status...${NC}"
if ! git diff-index --quiet HEAD --; then
    echo -e "${YELLOW}⚠️  Working directory has uncommitted changes${NC}"
    echo "Please commit or stash changes before releasing"
    git status --short
    exit 1
fi
echo -e "${GREEN}✓ Working directory clean${NC}"
echo ""

# Step 3: Verify version file exists and show current version
CURRENT_VERSION=$(grep -oP '(?<=EVERBLU_FW_VERSION ")\d+\.\d+\.\d+' src/core/version.h)
echo -e "${YELLOW}Current version: ${CURRENT_VERSION}${NC}"
echo -e "${YELLOW}New version: ${VERSION_NUMBER}${NC}"
echo ""

# Step 4: Update version.h
echo -e "${YELLOW}Updating src/core/version.h...${NC}"
if [ "$(uname)" = "Darwin" ]; then
    # macOS requires -i '' for sed
    sed -i '' "s/EVERBLU_FW_VERSION \"${CURRENT_VERSION}\"/EVERBLU_FW_VERSION \"${VERSION_NUMBER}\"/" src/core/version.h
else
    # Linux
    sed -i "s/EVERBLU_FW_VERSION \"${CURRENT_VERSION}\"/EVERBLU_FW_VERSION \"${VERSION_NUMBER}\"/" src/core/version.h
fi
echo -e "${GREEN}✓ Updated version.h${NC}"
echo ""

# Step 5: Show diff
echo -e "${YELLOW}Changes to src/core/version.h:${NC}"
git diff src/core/version.h || true
echo ""

# Step 6: Verify the change
UPDATED_VERSION=$(grep -oP '(?<=EVERBLU_FW_VERSION ")\d+\.\d+\.\d+' src/core/version.h)
if [ "$UPDATED_VERSION" != "$VERSION_NUMBER" ]; then
    echo -e "${YELLOW}❌ Version not updated correctly${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Version updated successfully${NC}"
echo ""

# Step 7: Run ESPHome release build
echo -e "${YELLOW}Running ESPHome release build...${NC}"
if [ -f "ESPHOME/prepare-component-release.sh" ]; then
    bash ESPHOME/prepare-component-release.sh
    echo -e "${GREEN}✓ ESPHome release built${NC}"
else
    echo -e "${YELLOW}⚠️  ESPHome release script not found (skipping)${NC}"
fi
echo ""

# Step 8: Show next steps
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}✓ Release preparation complete!${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "Next steps:"
echo "1. Review CHANGELOG.md and add entry for version $NEW_VERSION"
echo "   Format:"
echo "   ## [$NEW_VERSION] - YYYY-MM-DD"
echo "   ### Added"
echo "   ### Changed"
echo "   ### Fixed"
echo ""
echo "2. Commit changes:"
echo "   git add src/core/version.h CHANGELOG.md"
echo "   git commit -m \"chore: bump version to $NEW_VERSION\""
echo ""
echo "3. Create git tag:"
echo "   git tag -a $NEW_VERSION -m \"Release $NEW_VERSION\""
echo ""
echo "4. Push to remote:"
echo "   git push origin main"
echo "   git push origin $NEW_VERSION"
echo ""
echo "5. Create GitHub Release with CHANGELOG entries"
echo ""
