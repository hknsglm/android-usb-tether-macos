#!/bin/bash
set -e

BOLD='\033[1m'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

info()  { echo -e "${BOLD}[*]${NC} $1"; }
ok()    { echo -e "${GREEN}[+]${NC} $1"; }
fail()  { echo -e "${RED}[-]${NC} $1"; exit 1; }

echo ""
echo -e "${BOLD}=== Android USB Tethering for macOS ===${NC}"
echo ""

# Check for Homebrew
command -v brew &>/dev/null || fail "Homebrew is required. Install from https://brew.sh"
ok "Homebrew found"

# Install libusb
if ! brew list libusb &>/dev/null; then
    info "Installing libusb..."
    brew install libusb
else
    ok "libusb installed"
fi

# Check for Xcode CLI tools
xcode-select -p &>/dev/null || fail "Xcode Command Line Tools required: xcode-select --install"

# Build
info "Building..."
make clean 2>/dev/null || true
make || fail "Build failed"
ok "Build successful"

# Install
info "Installing to /Applications (requires sudo)..."
sudo make install

# Create default config
CONFIG_DIR="$HOME/.config/android-tether"
if [ ! -d "$CONFIG_DIR" ]; then
    mkdir -p "$CONFIG_DIR"
    cat > "$CONFIG_DIR/config" << 'CONF'
# Android USB Tethering Configuration
[network]
# no_route = false
# no_dns = false

[logging]
# level = info
CONF
    ok "Created config at $CONFIG_DIR/config"
fi

echo ""
echo -e "${GREEN}${BOLD}Done!${NC} The tethering daemon is now running in the background."
echo ""
echo "  Open ${BOLD}AndroidTether.app${NC} from /Applications for the menu bar UI."
echo "  The daemon auto-connects when an Android device with USB tethering is detected."
echo "  No password prompts needed!"
echo ""
echo "  To uninstall: sudo make uninstall"
echo ""
