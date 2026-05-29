// bridge/Sources/StopwatchBridge/BridgeService.swift
import Foundation

/// Top-level coordinator. Owns the supervisor, the codexbar client, and the GATT peripheral.
public actor BridgeService {

    private let config: Config
    private let supervisor: CodexbarSupervisor
    private let client: CodexbarClient
    private let peripheral: GATTPeripheral
    private var snapshotCache = SnapshotCache()
    private var costCache = CostCache()

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
            // Re-arm advertising first: a system sleep can silently kill it without
            // any CoreBluetooth callback, leaving the watch unable to find us.
            await peripheral.ensureAdvertising()
            await handleRefresh(scope: 0)
            // Refresh every 60s in the background.
            try? await Task.sleep(nanoseconds: 60_000_000_000)
        }
    }

    fileprivate func handleRefresh(scope: UInt8) async {
        if scope == Protocol.triggerScopeCost {
            await handleCostRefresh()
            return
        }
        let s = CodexbarClient.Scope(rawByte: scope)
        let started = Date()
        do {
            let usage = try await client.fetch(scope: s)
            let bytes = snapshotCache.recordSuccess(usage)
            await peripheral.updateSnapshot(bytes)
            let elapsed = Date().timeIntervalSince(started)
            FileHandle.standardOutput.write(Data(String(format: "fetch ok: scope=%d providers=%d %.1fs\n",
                                                        Int(scope), usage.providers.count, elapsed).utf8))
        } catch {
            if CodexbarClient.isCancellation(error) {
                // A newer trigger superseded this fetch (trigger writes are serialized by
                // cancelling the in-flight one). Not a failure — the superseding refresh
                // updates the snapshot; don't mark stale or cascade to a cost fetch.
                FileHandle.standardOutput.write(Data("fetch superseded by newer trigger (scope=\(scope))\n".utf8))
                return
            }
            FileHandle.standardError.write(Data("fetch failed: \(error)\n".utf8))
            await peripheral.updateSnapshot(snapshotCache.recordFailure())
        }
        // Refresh cost only on a full (all-providers) refresh / prewarm; narrow per-provider
        // usage triggers don't need a full /cost re-fetch (codexbar /cost is slow). Scope 0x04
        // remains the explicit cost-only path (handled by the early return above).
        if scope == 0 { await handleCostRefresh() }
    }

    private func handleCostRefresh() async {
        do {
            let cost = try await client.fetchCost(scope: .all)
            await peripheral.updateCostSnapshot(costCache.recordSuccess(cost))
            FileHandle.standardOutput.write(Data("cost ok: providers=\(cost.providers.count)\n".utf8))
        } catch {
            if CodexbarClient.isCancellation(error) {
                FileHandle.standardOutput.write(Data("cost fetch superseded by newer trigger\n".utf8))
                return
            }
            FileHandle.standardError.write(Data("cost fetch failed: \(error)\n".utf8))
            await peripheral.updateCostSnapshot(costCache.recordFailure())
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
