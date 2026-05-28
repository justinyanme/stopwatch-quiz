// bridge/Sources/StopwatchBridge/CodexbarSupervisor.swift
import Foundation

/// Spawns and restarts `codexbar serve --port <port>` as a child process.
/// Exponential backoff caps at 30 seconds.
public actor CodexbarSupervisor {

    public init(port: UInt16, codexbarPath: String? = nil) {
        self.port = port
        self.codexbarPath = codexbarPath ?? Self.findCodexbar()
    }

    public func start() {
        guard task == nil else { return }
        task = Task { await runLoop() }
    }

    public func stop() {
        task?.cancel()
        task = nil
        process?.terminate()
        process = nil
    }

    // MARK: - Internals

    private func runLoop() async {
        var backoff: TimeInterval = 1
        while !Task.isCancelled {
            guard let path = codexbarPath else {
                FileHandle.standardError.write(Data("codexbar binary not found on PATH; supervisor idle\n".utf8))
                try? await Task.sleep(nanoseconds: UInt64(30 * 1_000_000_000))
                continue
            }
            let proc = Process()
            proc.executableURL = URL(fileURLWithPath: path)
            proc.arguments = ["serve", "--port", String(port)]
            self.process = proc

            do {
                try proc.run()
                proc.waitUntilExit()
                FileHandle.standardError.write(Data("codexbar serve exited (status \(proc.terminationStatus)); restarting in \(backoff)s\n".utf8))
            } catch {
                FileHandle.standardError.write(Data("codexbar serve failed to start: \(error); backing off \(backoff)s\n".utf8))
            }
            try? await Task.sleep(nanoseconds: UInt64(backoff * 1_000_000_000))
            backoff = min(backoff * 2, 30)
        }
    }

    private static func findCodexbar() -> String? {
        let candidates = [
            "/opt/homebrew/bin/codexbar",
            "/usr/local/bin/codexbar",
            "/Applications/CodexBar.app/Contents/Helpers/CodexBarCLI",
        ]
        return candidates.first { FileManager.default.isExecutableFile(atPath: $0) }
    }

    private let port: UInt16
    private let codexbarPath: String?
    private var task: Task<Void, Never>?
    private var process: Process?
}
