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
                FileHandle.standardError.write(Data("codexbar binary not found in known locations; supervisor idle\n".utf8))
                try? await Task.sleep(nanoseconds: UInt64(30 * 1_000_000_000))
                continue
            }
            let proc = Process()
            proc.executableURL = URL(fileURLWithPath: path)
            proc.arguments = ["serve", "--port", String(port)]

            let startedAt = Date()
            do {
                try proc.run()
            } catch {
                FileHandle.standardError.write(Data("codexbar serve failed to start: \(error); backing off \(backoff)s\n".utf8))
                try? await Task.sleep(nanoseconds: UInt64(backoff * 1_000_000_000))
                backoff = min(backoff * 2, 30)
                continue
            }
            // Only record the process AFTER successful launch so stop() doesn't
            // call terminate() on an unstarted Process (NSInternalInconsistencyException).
            self.process = proc

            // Wait for the child to exit as a real async suspension point so
            // (1) the actor stays responsive, (2) the cooperative thread is freed,
            // (3) task cancellation actually terminates the child.
            await withTaskCancellationHandler {
                await withCheckedContinuation { (cont: CheckedContinuation<Void, Never>) in
                    proc.terminationHandler = { _ in cont.resume() }
                }
            } onCancel: {
                proc.terminate()
            }

            self.process = nil
            let ranFor = Date().timeIntervalSince(startedAt)
            FileHandle.standardError.write(Data("codexbar serve exited (status \(proc.terminationStatus)) after \(Int(ranFor))s\n".utf8))

            // Reset backoff after a healthy run (>10s) so the next crash gets
            // a fast restart, not a 30s penalty for the prior uptime.
            if ranFor > 10 {
                backoff = 1
            }
            if !Task.isCancelled {
                FileHandle.standardError.write(Data("restarting in \(backoff)s\n".utf8))
                try? await Task.sleep(nanoseconds: UInt64(backoff * 1_000_000_000))
                backoff = min(backoff * 2, 30)
            }
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
