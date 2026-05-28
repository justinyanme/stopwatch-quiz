// bridge/Sources/StopwatchBridge/InstallCommand.swift
import Foundation

enum InstallCommand {
    static let plistLabel = "dev.stopwatch.bridge"

    static func run() -> Int32 {
        // 1. Ensure config exists (creates default with random port if needed).
        let cfg: Config
        do {
            if let loaded = try Config.load() {
                cfg = loaded
                print("re-using existing config at \(Config.defaultPath.path)")
            } else {
                cfg = Config.makeDefault()
                try Config.save(cfg)
                print("created config at \(Config.defaultPath.path) (port \(cfg.codexbarPort))")
            }
        } catch {
            FileHandle.standardError.write(Data("config write failed: \(error)\n".utf8))
            return 1
        }

        // 2. Resolve the release binary path. The user is expected to have
        //    built with `swift build -c release` (the Makefile does this).
        let binary = FileManager.default.currentDirectoryPath + "/.build/release/stopwatch-bridge"
        guard FileManager.default.isExecutableFile(atPath: binary) else {
            FileHandle.standardError.write(Data("missing release binary at \(binary); run `swift build -c release` first\n".utf8))
            return 1
        }

        // 3. Write the launchd plist.
        let plistURL = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/LaunchAgents/\(plistLabel).plist")
        let plist = """
        <?xml version="1.0" encoding="UTF-8"?>
        <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
        <plist version="1.0">
        <dict>
          <key>Label</key><string>\(plistLabel)</string>
          <key>ProgramArguments</key>
          <array>
            <string>\(binary)</string>
            <string>run</string>
          </array>
          <key>RunAtLoad</key><true/>
          <key>KeepAlive</key><true/>
          <key>LimitLoadToSessionType</key><string>Aqua</string>
          <key>StandardOutPath</key><string>/tmp/stopwatch-bridge.log</string>
          <key>StandardErrorPath</key><string>/tmp/stopwatch-bridge.log</string>
        </dict>
        </plist>
        """
        do {
            try FileManager.default.createDirectory(at: plistURL.deletingLastPathComponent(),
                                                    withIntermediateDirectories: true)
            try plist.write(to: plistURL, atomically: true, encoding: .utf8)
            print("wrote launchd plist to \(plistURL.path)")
        } catch {
            FileHandle.standardError.write(Data("plist write failed: \(error)\n".utf8))
            return 1
        }

        // 4. Bootstrap into launchd (idempotent: unload first, ignore errors).
        _ = shell("/bin/launchctl", "bootout", "gui/\(getuid())", plistURL.path)
        let result = shell("/bin/launchctl", "bootstrap", "gui/\(getuid())", plistURL.path)
        if result != 0 {
            FileHandle.standardError.write(Data("launchctl bootstrap exited \(result); run manually: launchctl bootstrap gui/$UID \(plistURL.path)\n".utf8))
            return result
        }

        print("""

        Installed. Next steps:
          1. macOS will prompt for Bluetooth permission on first launch.
             If you don't see a prompt, open System Settings → Privacy & Security → Bluetooth
             and ensure stopwatch-bridge is allowed.
          2. Tail logs: tail -f /tmp/stopwatch-bridge.log
          3. To pair, press a button on the watch (after flashing firmware).
        """)
        return 0
    }

    private static func shell(_ cmd: String, _ args: String...) -> Int32 {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: cmd)
        p.arguments = args
        do {
            try p.run()
            p.waitUntilExit()
            return p.terminationStatus
        } catch {
            return -1
        }
    }
}
