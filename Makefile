CC = clang
CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS =

# Version from git (or "dev" if not in a git repo)
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "dev")
CFLAGS += -DVERSION=\"$(VERSION)\"

# macOS Universal Binary Support
ARCH_FLAGS ?= $(shell uname -m | grep -q 'arm64' && echo '-arch arm64' || echo '-arch x86_64')
ifeq ($(UNIVERSAL), 1)
	ARCH_FLAGS = -arch arm64 -arch x86_64
endif

# Minimum macOS version (10.15 Catalina)
CFLAGS += -mmacosx-version-min=10.15
LDFLAGS += -mmacosx-version-min=10.15
MAC_SDK := $(shell xcrun --show-sdk-path)
ifeq ($(UNIVERSAL), 1)
	SWIFT_FLAGS = -O -sdk $(MAC_SDK) -target arm64-apple-macos11.0 -target x86_64-apple-macos11.0
else
	SWIFT_FLAGS = -O -sdk $(MAC_SDK)
endif

# libusb from Homebrew
LIBUSB_PREFIX := $(shell brew --prefix libusb 2>/dev/null || echo /opt/homebrew)
CFLAGS += -I$(LIBUSB_PREFIX)/include/libusb-1.0 $(ARCH_FLAGS)
LDFLAGS += -L$(LIBUSB_PREFIX)/lib -lusb-1.0 $(ARCH_FLAGS)

# macOS frameworks
LDFLAGS += -framework CoreFoundation -framework IOKit

SRCDIR = src
BUILDDIR = build
RESDIR = resources

SRCS = $(SRCDIR)/main.c $(SRCDIR)/rndis.c $(SRCDIR)/usb_device.c $(SRCDIR)/utun.c \
       $(SRCDIR)/log.c $(SRCDIR)/dhcp.c $(SRCDIR)/arp.c $(SRCDIR)/frame.c $(SRCDIR)/stats.c \
       $(SRCDIR)/proto_rndis.c $(SRCDIR)/config.c $(SRCDIR)/ipc.c $(SRCDIR)/compat.c
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

# Everything lives inside one .app bundle
APP = $(BUILDDIR)/AndroidTether.app
APP_MACOS = $(APP)/Contents/MacOS
DAEMON = $(APP_MACOS)/android-tether
UI = $(APP_MACOS)/AndroidTetherUI

# Optional code signing
ifdef SIGN_IDENTITY
CODESIGN_CMD = codesign --force --sign "$(SIGN_IDENTITY)" --entitlements $(RESDIR)/entitlements.plist
else
CODESIGN_CMD = @true
endif

.PHONY: all clean install uninstall version

all: $(APP)

version:
	@echo "android-tether $(VERSION)"

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Build the single self-contained .app bundle
$(APP): $(OBJS) ui/MenuApp.swift $(RESDIR)/Info.plist | $(BUILDDIR)
	@# Create bundle structure
	mkdir -p $(APP_MACOS)
	@# Link daemon binary
	$(CC) $(OBJS) $(LDFLAGS) -o $(DAEMON)
	@# Compile Swift UI
	swiftc $(SWIFT_FLAGS) ui/MenuApp.swift -o $(UI)
	@# Copy Info.plist
	cp $(RESDIR)/Info.plist $(APP)/Contents/Info.plist
	@# Optional code signing
	$(CODESIGN_CMD) $(APP)
	@echo ""
	@echo "Build complete: $(APP) ($(VERSION))"
	@echo "  Daemon: $(DAEMON)"
	@echo "  UI:     $(UI)"

clean:
	rm -rf $(BUILDDIR)

PLIST_SRC = $(RESDIR)/com.hakansaglam.android-tether.plist
PLIST_DST = /Library/LaunchDaemons/com.hakansaglam.android-tether.plist

install: $(APP)
	@echo "Installing AndroidTether.app to /Applications..."
	@# Stop existing daemon if running
	launchctl unload $(PLIST_DST) 2>/dev/null || true
	rm -rf /Applications/AndroidTether.app
	cp -R $(APP) /Applications/
	@echo "Installing LaunchDaemon..."
	cp $(PLIST_SRC) $(PLIST_DST)
	chown root:wheel $(PLIST_DST)
	chmod 644 $(PLIST_DST)
	launchctl load $(PLIST_DST)
	@echo ""
	@echo "Installed! The daemon is now running in watch mode."
	@echo "Open AndroidTether.app from /Applications for the menu bar UI."
	@echo "No password prompts needed — the daemon runs as a system service."

uninstall:
	launchctl unload $(PLIST_DST) 2>/dev/null || true
	rm -f $(PLIST_DST)
	rm -rf /Applications/AndroidTether.app
	rm -f /tmp/android-tether.sock
	@echo "Uninstalled AndroidTether.app and LaunchDaemon"
