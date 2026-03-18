import Cocoa
import Foundation

// MARK: - Data Models

/// Stats from JSON file (legacy) or IPC
struct Stats: Codable {
    var tx_mbps: Double
    var rx_mbps: Double
    var tx_bytes_total: UInt64?
    var rx_bytes_total: UInt64?
    var tx_pkts_total: UInt64?
    var rx_pkts_total: UInt64?
    // IPC field names
    var tx_bytes: UInt64?
    var rx_bytes: UInt64?
    var tx_pkts: UInt64?
    var rx_pkts: UInt64?

    var totalTxBytes: UInt64 { tx_bytes_total ?? tx_bytes ?? 0 }
    var totalRxBytes: UInt64 { rx_bytes_total ?? rx_bytes ?? 0 }
}

struct IPCMessage: Codable {
    var type: String
    var state: String?
    var ip: String?
    var iface: String?
    var tx_mbps: Double?
    var rx_mbps: Double?
    var tx_bytes: UInt64?
    var rx_bytes: UInt64?
    var tx_pkts: UInt64?
    var rx_pkts: UInt64?
}

// MARK: - IPC Client

class IPCClient {
    private var sockFd: Int32 = -1
    private let sockPath = "/tmp/android-tether.sock"
    private var readBuffer = ""

    var onStats: ((Stats) -> Void)?
    var onState: ((String, String?, String?) -> Void)?

    func connect() -> Bool {
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { return false }

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        withUnsafeMutablePointer(to: &addr.sun_path) { ptr in
            sockPath.withCString { cstr in
                _ = memcpy(ptr, cstr, min(sockPath.count + 1, 104))
            }
        }

        let result = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sockPtr in
                Darwin.connect(fd, sockPtr, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }

        if result < 0 {
            close(fd)
            return false
        }

        // Set non-blocking AFTER successful connect
        let flags = fcntl(fd, F_GETFL, 0)
        if flags >= 0 {
            _ = fcntl(fd, F_SETFL, flags | O_NONBLOCK)
        }

        sockFd = fd
        readBuffer = ""
        eagainCount = 0

        return true
    }

    /// Tracks consecutive EAGAIN reads (no data available yet - normal)
    private var eagainCount = 0
    /// Tracks consecutive EOF reads (daemon died)
    private var eofCount = 0

    func poll() {
        guard sockFd >= 0 else { return }

        var buf = [UInt8](repeating: 0, count: 4096)
        let n = Darwin.read(sockFd, &buf, buf.count)

        if n > 0 {
            eagainCount = 0
            eofCount = 0
            readBuffer += String(bytes: buf[0..<n], encoding: .utf8) ?? ""

            // Process complete lines
            while let newlineRange = readBuffer.range(of: "\n") {
                let line = String(readBuffer[readBuffer.startIndex..<newlineRange.lowerBound])
                readBuffer = String(readBuffer[newlineRange.upperBound...])

                guard !line.isEmpty,
                      let lineData = line.data(using: .utf8),
                      let msg = try? JSONDecoder().decode(IPCMessage.self, from: lineData) else {
                    continue
                }

                if msg.type == "stats", let tx = msg.tx_mbps, let rx = msg.rx_mbps {
                    let stats = Stats(
                        tx_mbps: tx, rx_mbps: rx,
                        tx_bytes_total: nil, rx_bytes_total: nil,
                        tx_pkts_total: nil, rx_pkts_total: nil,
                        tx_bytes: msg.tx_bytes, rx_bytes: msg.rx_bytes,
                        tx_pkts: msg.tx_pkts, rx_pkts: msg.rx_pkts
                    )
                    onStats?(stats)
                } else if msg.type == "state" {
                    onState?(msg.state ?? "unknown", msg.ip, msg.iface)
                }
            }
        } else if n == 0 {
            // EOF - daemon closed connection
            eofCount += 1
            if eofCount >= 3 {
                disconnect()
                onState?("disconnected", nil, nil)
            }
        } else {
            // n < 0: check errno
            let err = errno
            if err == EAGAIN || err == EWOULDBLOCK {
                // No data available right now - totally normal for non-blocking
                eagainCount += 1
                // Don't disconnect on EAGAIN - it just means no data yet
            } else {
                // Real error - connection broken
                disconnect()
                onState?("disconnected", nil, nil)
            }
        }
    }

    func sendCommand(_ command: String) {
        guard sockFd >= 0 else { return }
        let msg = "{\"type\":\"\(command)\"}\n"
        msg.withCString { cstr in
            _ = Darwin.write(sockFd, cstr, Int(strlen(cstr)))
        }
    }

    func sendStop() {
        sendCommand("stop")
    }

    func disconnect() {
        if sockFd >= 0 {
            close(sockFd)
            sockFd = -1
        }
        readBuffer = ""
    }

    var isConnected: Bool { sockFd >= 0 }
}

// MARK: - App Delegate

class AppDelegate: NSObject, NSApplicationDelegate {
    var statusItem: NSStatusItem!
    var timer: Timer?
    var ipcClient = IPCClient()

    // Connection tracking
    var connectionState: String = "disconnected"
    var connectedSince: Date?

    // Menu items
    var startMenuItem: NSMenuItem!
    var stopMenuItem: NSMenuItem!
    var statsMenuItem: NSMenuItem!
    var totalDataMenuItem: NSMenuItem!
    var stateMenuItem: NSMenuItem!
    var durationMenuItem: NSMenuItem!
    var autoConnectMenuItem: NSMenuItem!
    var separatorBeforeActions: NSMenuItem!
    var autoConnectEnabled = true

    func applicationDidFinishLaunching(_ aNotification: Notification) {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        if let button = statusItem.button {
            button.title = "⚡ Tether"
            button.toolTip = "Android USB Tethering"
        }

        buildMenu()

        // Set up IPC callbacks
        ipcClient.onStats = { [weak self] stats in
            DispatchQueue.main.async {
                self?.updateDisplay(stats: stats)
            }
        }
        ipcClient.onState = { [weak self] state, ip, iface in
            DispatchQueue.main.async {
                self?.updateState(state: state, ip: ip, iface: iface)
            }
        }

        timer = Timer.scheduledTimer(timeInterval: 1.0, target: self,
                                     selector: #selector(tick), userInfo: nil, repeats: true)
    }

    func buildMenu() {
        let menu = NSMenu()

        stateMenuItem = NSMenuItem(title: "⏹ Disconnected", action: nil, keyEquivalent: "")
        stateMenuItem.isEnabled = false
        menu.addItem(stateMenuItem)

        durationMenuItem = NSMenuItem(title: "", action: nil, keyEquivalent: "")
        durationMenuItem.isEnabled = false
        durationMenuItem.isHidden = true
        menu.addItem(durationMenuItem)

        statsMenuItem = NSMenuItem(title: "", action: nil, keyEquivalent: "")
        statsMenuItem.isEnabled = false
        statsMenuItem.isHidden = true
        menu.addItem(statsMenuItem)

        totalDataMenuItem = NSMenuItem(title: "", action: nil, keyEquivalent: "")
        totalDataMenuItem.isEnabled = false
        totalDataMenuItem.isHidden = true
        menu.addItem(totalDataMenuItem)

        separatorBeforeActions = NSMenuItem.separator()
        menu.addItem(separatorBeforeActions)

        startMenuItem = NSMenuItem(title: "Start Tethering", action: #selector(startTethering), keyEquivalent: "s")
        startMenuItem.target = self
        menu.addItem(startMenuItem)

        stopMenuItem = NSMenuItem(title: "Stop Tethering", action: #selector(stopTethering), keyEquivalent: "x")
        stopMenuItem.target = self
        stopMenuItem.isHidden = true
        menu.addItem(stopMenuItem)

        menu.addItem(NSMenuItem.separator())

        autoConnectMenuItem = NSMenuItem(title: "Auto-Connect", action: #selector(toggleAutoConnect(_:)), keyEquivalent: "")
        autoConnectMenuItem.target = self
        autoConnectMenuItem.state = .on
        menu.addItem(autoConnectMenuItem)

        let openAtLoginItem = NSMenuItem(title: "Open at Login", action: #selector(toggleOpenAtLogin(_:)), keyEquivalent: "")
        openAtLoginItem.target = self
        openAtLoginItem.state = isOpenAtLoginEnabled() ? .on : .off
        menu.addItem(openAtLoginItem)

        menu.addItem(NSMenuItem.separator())

        let quitItem = NSMenuItem(title: "Quit", action: #selector(quitApp), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)

        statusItem.menu = menu
    }

    // MARK: - Timer Tick

    /// Track consecutive IPC failures to detect daemon death
    var ipcFailCount = 0

    @objc func tick() {
        // Update duration display
        if let since = connectedSince {
            let elapsed = Int(Date().timeIntervalSince(since))
            let hours = elapsed / 3600
            let minutes = (elapsed % 3600) / 60
            let seconds = elapsed % 60
            if hours > 0 {
                durationMenuItem.title = String(format: "⏱ Connected %d:%02d:%02d", hours, minutes, seconds)
            } else {
                durationMenuItem.title = String(format: "⏱ Connected %d:%02d", minutes, seconds)
            }
            durationMenuItem.isHidden = false
        }

        // Already connected via IPC - poll for updates
        if ipcClient.isConnected {
            ipcClient.poll()
            ipcFailCount = 0
            return
        }

        // Try to connect via IPC (daemon should always be running via LaunchDaemon)
        if ipcClient.connect() {
            ipcFailCount = 0
            // Connected to daemon — wait for it to send state messages
            return
        }

        // IPC failed - daemon is not running
        ipcFailCount += 1

        if connectionState != "disconnected" && ipcFailCount >= 3 {
            updateState(state: "disconnected", ip: nil, iface: nil)
        }
    }

    // MARK: - Display Updates

    func updateDisplay(stats: Stats) {
        statusItem.button?.title = String(format: "📶 ↓%.1f ↑%.1f", stats.rx_mbps, stats.tx_mbps)

        let txMB = Double(stats.totalTxBytes) / 1_048_576.0
        let rxMB = Double(stats.totalRxBytes) / 1_048_576.0

        if txMB >= 1024 || rxMB >= 1024 {
            statsMenuItem.title = String(format: "↓ %.2f GB  ↑ %.2f GB", rxMB / 1024.0, txMB / 1024.0)
        } else {
            statsMenuItem.title = String(format: "↓ %.1f MB  ↑ %.1f MB", rxMB, txMB)
        }
        statsMenuItem.isHidden = false

        // Total data (download + upload combined)
        let totalMB = txMB + rxMB
        if totalMB >= 1024 {
            totalDataMenuItem.title = String(format: "📊 Total: %.2f GB", totalMB / 1024.0)
        } else {
            totalDataMenuItem.title = String(format: "📊 Total: %.1f MB", totalMB)
        }
        totalDataMenuItem.isHidden = false
    }

    func updateState(state: String, ip: String?, iface: String?) {
        connectionState = state

        switch state {
        case "connected":
            let info = [ip, iface].compactMap { $0 }.joined(separator: " on ")
            if info.isEmpty {
                stateMenuItem.title = "✅ Connected"
            } else {
                stateMenuItem.title = "✅ Connected: \(info)"
            }
            statusItem.button?.title = "📶 Connected"
            startMenuItem.isHidden = true
            stopMenuItem.isHidden = false
            if connectedSince == nil {
                connectedSince = Date()
            }

        case "connecting":
            stateMenuItem.title = "⏳ Connecting..."
            statusItem.button?.title = "⏳ Connecting..."
            startMenuItem.isHidden = true
            stopMenuItem.isHidden = false

        case "watching":
            stateMenuItem.title = "👀 Watching for device..."
            statusItem.button?.title = "⚡ Watching"
            statsMenuItem.isHidden = true
            totalDataMenuItem.isHidden = true
            durationMenuItem.isHidden = true
            startMenuItem.isHidden = true
            stopMenuItem.isHidden = false
            connectedSince = nil
            autoConnectEnabled = true
            autoConnectMenuItem.state = .on

        case "idle":
            stateMenuItem.title = "⏸ Auto-connect disabled"
            statusItem.button?.title = "⚡ Tether"
            statsMenuItem.isHidden = true
            totalDataMenuItem.isHidden = true
            durationMenuItem.isHidden = true
            startMenuItem.isHidden = false
            stopMenuItem.isHidden = true
            connectedSince = nil
            autoConnectEnabled = false
            autoConnectMenuItem.state = .off

        case "disconnected":
            stateMenuItem.title = "⏹ Disconnected"
            statsMenuItem.isHidden = true
            totalDataMenuItem.isHidden = true
            durationMenuItem.isHidden = true
            statusItem.button?.title = "⚡ Tether"
            startMenuItem.isHidden = false
            stopMenuItem.isHidden = true
            connectedSince = nil
            ipcClient.disconnect()

        default:
            stateMenuItem.title = state
        }
    }

    // MARK: - Actions

    func applicationWillTerminate(_ aNotification: Notification) {
        ipcClient.disconnect()
    }

    @objc func startTethering() {
        // Send "enable" to daemon to resume auto-connect
        if ipcClient.isConnected {
            ipcClient.sendCommand("enable")
        } else if ipcClient.connect() {
            ipcClient.sendCommand("enable")
        } else {
            let alert = NSAlert()
            alert.messageText = "Daemon Not Running"
            alert.informativeText = "The tethering daemon is not running.\n\nReinstall with: sudo make install"
            alert.runModal()
            return
        }
        autoConnectEnabled = true
        autoConnectMenuItem.state = .on
    }

    @objc func stopTethering() {
        if ipcClient.isConnected {
            ipcClient.sendStop()
        }
    }

    @objc func toggleAutoConnect(_ sender: NSMenuItem) {
        autoConnectEnabled.toggle()
        sender.state = autoConnectEnabled ? .on : .off
        if ipcClient.isConnected {
            ipcClient.sendCommand(autoConnectEnabled ? "enable" : "disable")
        } else if ipcClient.connect() {
            ipcClient.sendCommand(autoConnectEnabled ? "enable" : "disable")
        }
    }

    @objc func quitApp() {
        ipcClient.disconnect()
        NSApplication.shared.terminate(nil)
    }

    // MARK: - Open at Login

    @objc func toggleOpenAtLogin(_ sender: NSMenuItem) {
/*
#if compiler(>=5.7) && canImport(ServiceManagement)
        if #available(macOS 13.0, *) {
            do {
                let service = SMAppService.mainApp
                if sender.state == .on {
                    try service.unregister()
                    sender.state = .off
                } else {
                    try service.register()
                    sender.state = .on
                }
            } catch {
                NSLog("Failed to toggle login item: \(error)")
            }
            return
        }
#endif
*/
        let alert = NSAlert()
        alert.messageText = "Open at Login"
        alert.informativeText = "Please add AndroidTether to System Settings → Login Items."
        alert.runModal()
    }

    func isOpenAtLoginEnabled() -> Bool {
/*
#if compiler(>=5.7) && canImport(ServiceManagement)
        if #available(macOS 13.0, *) {
            return SMAppService.mainApp.status == .enabled
        }
#endif
*/
        return false
    }

}

// MARK: - Entry Point

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()
