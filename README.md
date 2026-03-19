# Android USB Tethering for macOS

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![Platform: macOS](https://img.shields.io/badge/Platform-macOS%2010.15%2B-brightgreen.svg)
![Architecture: Universal](https://img.shields.io/badge/Arch-arm64%20%7C%20x86__64-orange.svg)
![Version: 2.0](https://img.shields.io/badge/Version-2.0-purple.svg)

A production-quality, extensible userspace driver that enables Android's USB tethering (RNDIS) on macOS, including Apple Silicon Macs. No kernel extensions or SIP changes required.

macOS does not natively support the RNDIS protocol that Android uses for USB tethering. This tool bridges that gap by communicating with the Android device over USB using `libusb` and creating a virtual network interface via macOS's `utun`.

## Features

- **Zero Password Prompts**: Runs as a LaunchDaemon — install once with `sudo`, never enter a password again
- **Auto-Connect**: Automatically detects USB tethering and connects — just plug in your phone
- **High Speed**: Async TX/RX libusb transfer pools (16 RX + 64 TX) for high-speed 5G tethering
- **Menu Bar App**: Live speed display, total data usage, connection duration, and auto-connect toggle
- **Graceful Cleanup**: Reliably restores macOS network routes and DNS on exit (even Ctrl+C)
- **Universal Binary**: Compiles for both Apple Silicon (`arm64`) and Intel (`x86_64`)
- **Safe Userspace**: Zero kernel extensions, no SIP changes needed
- **Extensible Architecture**: Protocol driver abstraction allows community to add NCM/ECM support
- **Single App Bundle**: Everything lives in one `.app` — drag to trash to uninstall
- **macOS 10.15+**: Supports Catalina through the latest macOS versions

## How It Works

```
Android Phone (RNDIS)  <-->  USB/libusb  <-->  android-tether  <-->  utun interface  <-->  macOS networking
```

1. Detects the Android RNDIS device on USB
2. Initializes the RNDIS protocol (init, query MAC, set packet filter)
3. Performs DHCP to obtain an IP address from the Android device
4. Creates a `utun` virtual network interface on macOS
5. Bridges Ethernet frames between USB and the utun interface using async transfers
6. Configures routing and DNS

## Requirements

- macOS 10.15+ (Catalina or later, Apple Silicon or Intel)
- [Homebrew](https://brew.sh)
- An Android device with USB tethering enabled

## Installation

### Download (Recommended)

Download the latest `.pkg` from [Releases](../../releases), double-click to install. Done.

No Homebrew, no Xcode, no Terminal required.

### Build from Source

```bash
# Install dependencies
brew install libusb

# Build and install (one-time sudo)
make
sudo make install

# Or build a .pkg installer yourself
make pkg
```

### Uninstall

```bash
sudo make uninstall
```

Or manually:
```bash
sudo launchctl unload /Library/LaunchDaemons/com.hakansaglam.android-tether.plist
sudo rm /Library/LaunchDaemons/com.hakansaglam.android-tether.plist
rm -rf /Applications/AndroidTether.app
```

## Usage

### Menu Bar App (Recommended)

Open **AndroidTether.app** from `/Applications`. The menu bar shows:
- Live download/upload speed (e.g. `📶 ↓12.3 ↑2.1`)
- Connection status, duration, and total data usage
- Auto-Connect toggle — disable to stop automatic connections
- Stop button to disconnect the current session

Just plug in your Android phone with USB tethering enabled — it connects automatically.

### Command Line (Advanced)

You can also run the daemon directly for debugging:

```bash
# One-shot mode
sudo ./build/AndroidTether.app/Contents/MacOS/android-tether

# Watch mode (auto-connect, like the LaunchDaemon)
sudo ./build/AndroidTether.app/Contents/MacOS/android-tether --watch
```

### Options

| Flag | Description |
|------|-------------|
| `-n, --no-route` | Don't set up default route |
| `-d, --no-dns` | Don't modify DNS settings |
| `-s, --static IP` | Use a static IP instead of DHCP |
| `-g, --gateway IP` | Set gateway (with `--static`) |
| `-m, --netmask IP` | Set netmask (default: 255.255.255.0) |
| `-w, --watch` | Watch mode: auto-connect when device detected |
| `-v, --verbose` | Debug-level logging |
| `-V, --version` | Show version |
| `-c, --config FILE` | Use custom config file |

### Configuration File

Create `~/.config/android-tether/config`:

```ini
[network]
no_route = false
no_dns = false
# static_ip = 192.168.42.50
# gateway = 192.168.42.129
netmask = 255.255.255.0

[logging]
level = info

[protocol]
driver = rndis
```

CLI arguments override config file settings.

### Examples

```bash
# Basic usage - auto-configures everything
sudo android-tether

# Don't change existing routing or DNS
sudo android-tether --no-route --no-dns

# Use static IP
sudo android-tether --static 192.168.42.50 --gateway 192.168.42.129

# Verbose mode for debugging
sudo android-tether --verbose
```

## Architecture

```
include/
  proto_driver.h    Protocol driver interface (RNDIS, NCM, ECM)
  net_types.h       Shared network types (Ethernet, ARP, DHCP)
  config.h          Configuration system
  ipc.h             Unix domain socket IPC
  log.h             Structured logging
  compat.h          macOS version compatibility
  rndis.h           RNDIS protocol definitions
  usb_device.h      USB device interface
  utun.h            macOS utun interface
  dhcp.h            DHCP client
  arp.h             ARP handler
  frame.h           Frame conversion (Ethernet <-> utun)
  stats.h           Statistics and legacy IPC

src/
  main.c            Session orchestration and bridge threads
  proto_rndis.c     RNDIS protocol driver (via proto_driver_t)
  rndis.c           Low-level RNDIS protocol implementation
  usb_device.c      USB device discovery and communication
  utun.c            macOS network interface management
  dhcp.c            DHCP client (raw packet construction)
  arp.c             ARP request/reply handling
  frame.c           Ethernet <-> utun packet conversion
  config.c          INI config file parser + CLI
  ipc.c             Unix domain socket server
  log.c             Logging system
  stats.c           Stats JSON writer
  compat.c          macOS version detection

ui/
  MenuApp.swift     Menu bar application with IPC support

resources/
  Info.plist                              App bundle metadata
  entitlements.plist                      Code signing entitlements
  com.hakansaglam.android-tether.plist    launchd daemon plist
```

## Extending: Adding a New Protocol Driver

To add support for a new USB tethering protocol (e.g., NCM or ECM):

1. Create `src/proto_ncm.c`
2. Implement the `proto_driver_t` interface (see `include/proto_driver.h`)
3. Add a `proto_ncm_create()` constructor
4. Register it in the driver lookup in `main.c`

The protocol driver interface provides callbacks for:
- `init()` - Protocol initialization over USB
- `get_mac()` - Get device MAC address
- `wrap_frame()` - Wrap Ethernet frame for USB transmission
- `unwrap_data()` - Extract Ethernet frames from USB data
- `keepalive()` - Periodic keepalive (optional)
- `get_usb_filters()` - USB interface class/subclass/protocol for device discovery

## Troubleshooting

**"No Android RNDIS device found"**
- Make sure USB tethering is enabled *before* running the tool
- Try unplugging and replugging the USB cable
- Some USB cables are charge-only; use a data cable

**"Daemon Not Running" in menu bar app**
- Run `sudo make install` to install the LaunchDaemon
- Check logs: `cat /tmp/android_tether.log`

**"Permission denied" or USB errors**
- The daemon must run as root (handled automatically by the LaunchDaemon)
- On macOS, you may need to allow the USB device in System Settings → Privacy & Security

**DHCP fails**
- Falls back to a default static IP (192.168.42.100)
- Use `--static` to set your own IP

**Connection drops**
- In watch mode, the daemon auto-reconnects within 3 seconds
- Check `/tmp/android_tether.log` for details

## License

MIT
