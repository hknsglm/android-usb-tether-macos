#!/bin/bash
set -e

BOLD='\033[1m'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

info()  { echo -e "${BOLD}[*]${NC} $1"; }
ok()    { echo -e "${GREEN}[+]${NC} $1"; }
fail()  { echo -e "${RED}[-]${NC} $1"; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
PKG_ROOT="$BUILD_DIR/pkg-root"
PKG_SCRIPTS="$ROOT_DIR/scripts"

VERSION=$(git -C "$ROOT_DIR" describe --tags --always --dirty 2>/dev/null || echo "dev")
PKG_NAME="AndroidTether-${VERSION}.pkg"

echo ""
echo -e "${BOLD}=== Building AndroidTether.pkg ($VERSION) ===${NC}"
echo ""

# Step 1: Build with static libusb
info "Building with static libusb..."
make -C "$ROOT_DIR" clean 2>/dev/null || true
make -C "$ROOT_DIR" STATIC=1 || fail "Build failed"
ok "Build successful"

# Step 2: Verify no dynamic libusb dependency
if otool -L "$BUILD_DIR/AndroidTether.app/Contents/MacOS/android-tether" | grep -q libusb; then
    fail "Binary still links to dynamic libusb! Static build failed."
fi
ok "No external dylib dependencies"

# Step 3: Create pkg staging area
info "Preparing package contents..."
rm -rf "$PKG_ROOT"
mkdir -p "$PKG_ROOT/Applications"
mkdir -p "$PKG_ROOT/Library/LaunchDaemons"

cp -R "$BUILD_DIR/AndroidTether.app" "$PKG_ROOT/Applications/"
cp "$ROOT_DIR/resources/com.hakansaglam.android-tether.plist" \
   "$PKG_ROOT/Library/LaunchDaemons/"

# Step 4: Generate component plist and disable relocation
info "Building installer package..."
COMPONENT_PLIST="$BUILD_DIR/component.plist"
pkgbuild --analyze --root "$PKG_ROOT" "$COMPONENT_PLIST"

# Disable bundle relocation so the app always goes to /Applications
/usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$COMPONENT_PLIST"

pkgbuild \
    --root "$PKG_ROOT" \
    --scripts "$PKG_SCRIPTS" \
    --identifier "com.hakansaglam.android-tether" \
    --version "$VERSION" \
    --install-location "/" \
    --component-plist "$COMPONENT_PLIST" \
    "$BUILD_DIR/$PKG_NAME"

rm -f "$COMPONENT_PLIST"

ok "Package built: build/$PKG_NAME"

# Step 5: Clean up staging
rm -rf "$PKG_ROOT"

echo ""
echo -e "${GREEN}${BOLD}Done!${NC} Installer: ${BOLD}build/$PKG_NAME${NC}"
echo ""
echo "Users just double-click the .pkg to install."
echo "No Homebrew, no Xcode, no Terminal required."
echo ""
