# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed
- Increased RNDIS Tx/Rx buffers to 128KB to fully support 5G speeds (>160 Mbps) by allowing Android devices to batch packets more efficiently.
- Increased in-flight USB async receive transfers from 16 to 32 to ensure maximum USB bus throughput.

## [2.0.0] - 2026-03-16

### Added
- **Protocol Driver Architecture**: Extensible `proto_driver_t` interface for adding NCM/ECM support
- **macOS Menu Bar App**: Native `NSStatusItem` UI with live speed display and Start/Stop controls
- **IPC System**: Unix domain socket for real-time communication between daemon and UI
- **Config File Support**: INI-style config at `~/.config/android-tether/config` with CLI overrides
- **Async TX Pool**: 64 async USB TX transfers for higher upload throughput
- **Structured Logging**: Tagged log system with timestamps and configurable log levels
- **Open at Login**: Menu bar app can register itself as a login item (macOS 13+)
- **Connection Duration**: UI shows elapsed connection time
- **macOS Compatibility Layer**: `compat.c` for version detection and conditional features
- **launchd Plist**: Optional launchd daemon configuration for background operation

### Changed
- Async RX transfers increased from 1 to 16 in-flight for higher download throughput
- Socket buffers increased to 4MB for better burst handling
- DHCP client improved with proper lease parsing
- Build system supports Universal Binary (arm64 + x86_64) via `make UNIVERSAL=1`
- Swift UI build now auto-detects host architecture

### Fixed
- Network routes and DNS are now reliably cleaned up on exit (atexit + signal handlers)
- File-based stop trigger prevents daemon from hanging on stop
- Guard route teardown with extra safety route removals

## [1.0.0] - 2026-03-10

### Added
- Initial release
- RNDIS protocol implementation for Android USB tethering on macOS
- Async USB RX transfers via libusb
- macOS utun virtual network interface
- DHCP client for automatic IP configuration
- ARP request/reply handling
- DNS configuration via scutil
- Split-tunnel routing (0.0.0.0/1 + 128.0.0.0/1)
- Auto-reconnect on device disconnect
- Command-line options for static IP, no-route, no-dns modes
- MIT License
