# Contributing to Android USB Tethering for macOS

Thank you for your interest in contributing! This guide will help you get started.

## Development Setup

### Prerequisites

- macOS 10.15+ (Catalina or later)
- Xcode Command Line Tools (`xcode-select --install`)
- [Homebrew](https://brew.sh)
- libusb (`brew install libusb`)

### Building

```bash
# Standard build (host architecture)
make clean && make

# Universal Binary (arm64 + x86_64)
make UNIVERSAL=1

# Verbose build output
make V=1
```

### Running

```bash
# Run the daemon (requires root)
sudo ./build/android-tether --verbose

# Install locally
sudo make install-all
```

## Project Structure

```
src/              C source files (daemon)
include/          C header files
ui/               Swift menu bar application
resources/        App bundle resources and plists
build/            Build output (gitignored)
```

### Key Components

| File | Purpose |
|------|---------|
| `src/main.c` | Session orchestration, bridge threads, main loop |
| `src/proto_rndis.c` | RNDIS protocol driver (implements `proto_driver_t`) |
| `src/usb_device.c` | USB device discovery and bulk transfers |
| `src/utun.c` | macOS utun interface, routing, DNS |
| `src/dhcp.c` | DHCP client (raw packet construction) |
| `src/ipc.c` | Unix domain socket server for UI communication |
| `ui/MenuApp.swift` | Native macOS menu bar app |

## Code Style

- **C code**: K&R style, 4-space indentation, no tabs
- **Naming**: `snake_case` for functions and variables, `UPPER_CASE` for constants
- **Headers**: Every `.c` file has a matching `.h` in `include/`
- **Logging**: Use `LOG_D()`, `LOG_I()`, `LOG_W()`, `LOG_E()` macros with a tag
- **Error handling**: Return -1 on error, 0 on success; log errors at the point they occur

## Adding a New Protocol Driver

The project uses a vtable-based protocol driver interface. To add support for a new USB tethering protocol (e.g., NCM or ECM):

1. **Create** `src/proto_ncm.c` and `include/proto_ncm.h` (if needed)
2. **Implement** the `proto_driver_t` interface (see `include/proto_driver.h`):
   - `init()` — Protocol initialization over USB
   - `get_mac()` — Get device MAC address
   - `wrap_frame()` — Wrap Ethernet frame for USB TX
   - `unwrap_data()` — Extract Ethernet frames from USB RX data
   - `keepalive()` — Periodic keepalive (optional)
   - `get_usb_filters()` — USB class/subclass/protocol for device discovery
   - `destroy()` — Cleanup
3. **Add** a `proto_ncm_create()` constructor
4. **Register** it in the driver lookup in `main.c`
5. **Update** the `Makefile` `SRCS` list

## Pull Request Process

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/ncm-support`)
3. Make your changes
4. Ensure `make clean && make` compiles with zero warnings
5. Test with a real device if possible
6. Submit a pull request with a clear description

## Reporting Issues

When reporting bugs, please include:

- macOS version (`sw_vers`)
- Hardware (Intel or Apple Silicon)
- Android device model
- Output of `sudo android-tether --verbose` (redact any sensitive info)
- Steps to reproduce
