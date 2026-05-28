// bridge/Sources/StopwatchBridge/BridgeService.swift
import Foundation

/// Top-level coordinator. Owns the supervisor, the codexbar client, and the GATT peripheral.
public actor BridgeService {

    private let config: Config
    private let supervisor: CodexbarSupervisor
    private let client: CodexbarClient
    private let peripheral: GATTPeripheral

    public init(config: Config) async {
        self.config = config
        self.supervisor = CodexbarSupervisor(port: config.codexbarPort)
        self.client = CodexbarClient(port: config.codexbarPort)
        // GATTPeripheral is @MainActor; constructing it from an async init hops to main.
        self.peripheral = await GATTPeripheral()
    }

    public func run() async {
        if config.spawnCodexbar {
            await supervisor.start()
        } else {
            FileHandle.standardOutput.write(Data("spawnCodexbar=false; expecting external codexbar serve on port \(config.codexbarPort)\n".utf8))
        }
        // Hop to main to assign delegate on the main-actor peripheral.
        await MainActor.run { [self] in
            self.peripheral.delegate = self
        }

        // Background prewarm loop: keeps the GATT snapshot fresh even when no
        // watch is connected. Watch reads always see the latest cached frame
        // (codexbar serve takes 5-15 s per call, so we cannot fetch on demand).
        Task { await self.prewarmLoop() }

        // Block forever so launchd doesn't reap us.
        await withCheckedContinuation { (_: CheckedContinuation<Void, Never>) in }
    }

    private func prewarmLoop() async {
        // Give codexbar serve a moment to come up before the first fetch.
        try? await Task.sleep(nanoseconds: 3_000_000_000)
        while !Task.isCancelled {
            await handleRefresh(scope: 0)
            // Refresh every 60s in the background.
            try? await Task.sleep(nanoseconds: 60_000_000_000)
        }
    }

    fileprivate func handleRefresh(scope: UInt8) async {
        let s = CodexbarClient.Scope(rawByte: scope)
        let started = Date()
        do {
            let usage = try await client.fetch(scope: s)
            let bytes = SnapshotEncoder.encode(usage)
            await peripheral.updateSnapshot(bytes)
            let elapsed = Date().timeIntervalSince(started)
            FileHandle.standardOutput.write(Data(String(format: "fetch ok: scope=%d providers=%d %.1fs\n",
                                                        Int(scope), usage.providers.count, elapsed).utf8))
        } catch {
            FileHandle.standardError.write(Data("fetch failed: \(error)\n".utf8))
            // Build an error snapshot with the bridge_error flag set, still 56 bytes (3 disabled providers).
            let errUsage = NormalizedUsage(
                capturedAt: Date(),
                flags: [.bridgeError, .stale],
                providers: [
                    .init(providerID: .codex,  status: .disabled, sessionPct: nil, weekPct: nil,
                          sessionResetAt: nil, weekResetAt: nil, credits: nil, plan: .unknown),
                    .init(providerID: .claude, status: .disabled, sessionPct: nil, weekPct: nil,
                          sessionResetAt: nil, weekResetAt: nil, credits: nil, plan: .unknown),
                    .init(providerID: .gemini, status: .disabled, sessionPct: nil, weekPct: nil,
                          sessionResetAt: nil, weekResetAt: nil, credits: nil, plan: .unknown),
                ]
            )
            await peripheral.updateSnapshot(SnapshotEncoder.encode(errUsage))
        }
    }
}

extension BridgeService: GATTPeripheralDelegate {
    public nonisolated func gattPeripheral(_ peripheral: GATTPeripheral, refreshRequestedFor scope: UInt8) async {
        await handleRefresh(scope: scope)
    }
}

private extension CodexbarClient.Scope {
    init(rawByte: UInt8) {
        switch rawByte {
        case 1: self = .codex
        case 2: self = .claude
        case 3: self = .gemini
        default: self = .all
        }
    }
}
