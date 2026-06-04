// bridge/Sources/StopwatchBridge/BridgeService.swift
import Foundation

/// Top-level coordinator. Owns the supervisor, the codexbar client, and the GATT peripheral.
public actor BridgeService {

    private let config: Config
    private let supervisor: CodexbarSupervisor
    private let client: CodexbarClient
    private let peripheral: GATTPeripheral
    private let repository: SnapshotRepository
    private let httpServer: LocalHTTPServer
    private var snapshotCache = SnapshotCache()
    private var costCache = CostCache()
    private let balanceClient: BalanceClient
    private var balanceCache = BalanceCache()
    private let usageClient: UsageClient
    private var usageCache = UsageCache()
    private let usageTargets: [UsageClient.Target]
    private let providers: [ProviderConfig.Resolved]
    private var lastPolled: [String: Date] = [:]
    private let directUsageCollector = DirectUsageCollector()
    private let directCostCollector = DirectCostCollector()

    public init(config: Config) async {
        self.config = config
        self.supervisor = CodexbarSupervisor(port: config.codexbarPort)
        self.client = CodexbarClient(port: config.codexbarPort)
        let loadedProviders = (try? ProvidersConfig.load())?.map { $0.resolved() } ?? []
        self.providers = loadedProviders
        self.balanceClient = BalanceClient(keyStore: KeychainStore())
        self.usageClient = UsageClient(keyStore: KeychainStore())
        self.usageTargets = loadedProviders.compactMap { p in
            guard let k = p.usageKind, let cid = p.usageCredentialID else { return nil }
            return UsageClient.Target(kind: k, credentialID: cid)
        }
        // GATTPeripheral is @MainActor; constructing it from an async init hops to main.
        self.peripheral = await GATTPeripheral()
        self.repository = SnapshotRepository()
        let repository = self.repository
        let refreshBridge = BridgeRefreshBridge()
        let scheduler = RefreshScheduler { scope in
            await refreshBridge.refresh(scope: scope)
        }
        self.httpServer = LocalHTTPServer(
            host: config.httpBindHost,
            port: config.httpPort,
            onEvent: { event in
                Self.writeHTTPServerEvent(event)
            },
            handler: SnapshotHTTPHandler(
                repository: repository,
                authenticator: HTTPAuthenticator(apiToken: config.apiToken),
                scheduler: scheduler
            ).handle)
        refreshBridge.service = self
    }

    /// The bridge now collects provider data directly (Codex/Claude/Gemini
    /// usage + local cost logs) and no longer spawns or depends on the CodexBar
    /// runtime, regardless of the legacy `spawnCodexbar` config flag.
    static func usesCodexbarRuntimeByDefault(spawnCodexbar: Bool) -> Bool {
        false
    }

    public func run() async {
        FileHandle.standardOutput.write(Data("codexbar runtime disabled; using direct collectors\n".utf8))
        // Hop to main to assign delegate on the main-actor peripheral.
        await MainActor.run { [self] in
            self.peripheral.delegate = self
        }
        httpServer.start()

        // Background prewarm loop: keeps the GATT snapshot fresh even when no
        // watch is connected. Watch reads always see the latest cached frame
        // (codexbar serve takes 5-15 s per call, so we cannot fetch on demand).
        Task { await self.prewarmLoop() }
        if !providers.isEmpty { Task { await self.balancePollLoop() } }

        // Block forever so launchd doesn't reap us; the prewarm loop and GATT
        // callbacks run on their own tasks. (A never-resumed continuation trips
        // Swift's "continuation leaked" runtime warning, so sleep in a loop instead.)
        while !Task.isCancelled {
            try? await Task.sleep(nanoseconds: 3_600_000_000_000)  // 1 hour
        }
    }

    static func shouldPublishRefreshResult(taskIsCancelled: Bool = Task.isCancelled) -> Bool {
        !taskIsCancelled
    }

    static func httpServerEventMessage(_ event: LocalHTTPServerEvent) -> BridgeServiceLogMessage {
        switch event {
        case let .listening(host, port):
            return .init(stream: .standardOutput, text: "http listening on http://\(host):\(port)\n")
        case let .failed(host, port, message):
            return .init(
                stream: .standardError,
                text: "http server failed on http://\(host):\(port): \(message)\n")
        }
    }

    private static func writeHTTPServerEvent(_ event: LocalHTTPServerEvent) {
        let message = httpServerEventMessage(event)
        switch message.stream {
        case .standardOutput:
            FileHandle.standardOutput.write(Data(message.text.utf8))
        case .standardError:
            FileHandle.standardError.write(Data(message.text.utf8))
        }
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
        if scope == Protocol.triggerScopeBalances {
            await handleBalanceRefresh(force: true)
            return
        }
        if scope == Protocol.triggerScopeUsage {
            await handleUsageRefresh()
            return
        }
        if scope == Protocol.triggerScopeCost {
            await handleCostRefresh()
            return
        }
        let started = Date()
        // Direct collectors fetch all configured providers at once; the scope
        // byte only distinguishes the cost/balance/usage triggers handled above.
        // An empty/failed result is preserved as stale by recordSuccess (which
        // keeps last-good numbers rather than wiping them).
        let usage = await directUsageCollector.fetchAll()
        let bytes = snapshotCache.recordSuccess(usage)
        guard Self.shouldPublishRefreshResult() else {
            FileHandle.standardOutput.write(Data("fetch superseded before publish (scope=\(scope))\n".utf8))
            return
        }
        await peripheral.updateSnapshot(bytes)
        await repository.update(.snapshot, bytes: bytes)
        let elapsed = Date().timeIntervalSince(started)
        FileHandle.standardOutput.write(Data(String(format: "fetch ok: scope=%d providers=%d %.1fs\n",
                                                    Int(scope), usage.providers.count, elapsed).utf8))
        // Refresh cost only on a full (all-providers) refresh / prewarm; narrow per-provider
        // usage triggers don't need a full cost re-fetch. Scope 0x04 remains the explicit
        // cost-only path (handled by the early return above).
        let usageSucceeded = !usage.providers.isEmpty && !usage.flags.contains(.bridgeError)
        if Self.shouldRefreshCostAfterUsage(scope: scope, usageSucceeded: usageSucceeded) {
            await handleCostRefresh()
        }
    }

    static func shouldRefreshCostAfterUsage(scope: UInt8, usageSucceeded: Bool) -> Bool {
        scope == 0 && usageSucceeded
    }

    private func handleUsageRefresh() async {
        guard !usageTargets.isEmpty else {
            let unavailable = UsageEncoder.unavailableEmpty()
            guard Self.shouldPublishRefreshResult() else {
                FileHandle.standardOutput.write(Data("usage refresh superseded before publish\n".utf8))
                return
            }
            await peripheral.updateUsageSnapshot(unavailable)
            await repository.update(.balanceUsage, bytes: unavailable)
            return
        }
        let fresh = await usageClient.fetchAll(usageTargets)
        let bytes = usageCache.recordSuccess(fresh)
        guard Self.shouldPublishRefreshResult() else {
            FileHandle.standardOutput.write(Data("usage refresh superseded before publish\n".utf8))
            return
        }
        await peripheral.updateUsageSnapshot(bytes)
        await repository.update(.balanceUsage, bytes: bytes)
        let summary = fresh.providers.map { "\($0.kind)=\($0.status)" }.joined(separator: " ")
        FileHandle.standardOutput.write(Data("usage ok: \(summary)\n".utf8))
    }

    private func handleCostRefresh() async {
        // Direct local cost collector scans Codex/Claude session logs; it never
        // throws. An empty result is preserved as stale by recordSuccess.
        let cost = await directCostCollector.fetchAll()
        let bytes = costCache.recordSuccess(cost)
        guard Self.shouldPublishRefreshResult() else {
            FileHandle.standardOutput.write(Data("cost refresh superseded before publish\n".utf8))
            return
        }
        await peripheral.updateCostSnapshot(bytes)
        await repository.update(.cost, bytes: bytes)
        FileHandle.standardOutput.write(Data("cost ok: providers=\(cost.providers.count)\n".utf8))
    }

    private func balancePollLoop() async {
        try? await Task.sleep(nanoseconds: 2_000_000_000)
        while !Task.isCancelled {
            await handleBalanceRefresh(force: false)
            try? await Task.sleep(nanoseconds: 30_000_000_000)   // 30 s tick; per-provider cadence below
        }
    }

    /// Polls providers whose `pollSeconds` has elapsed (or all, when `force`).
    private func handleBalanceRefresh(force: Bool) async {
        let now = Date()
        let due = providers.filter { p in
            force || (lastPolled[p.id].map { now.timeIntervalSince($0) >= Double(p.pollSeconds) } ?? true)
        }
        guard !due.isEmpty else { return }
        let fresh = await balanceClient.fetchAll(due, now: now)
        guard Self.shouldPublishRefreshResult() else {
            FileHandle.standardOutput.write(Data("balance refresh superseded before publish\n".utf8))
            return
        }
        for p in due { lastPolled[p.id] = now }
        let bytes = balanceCache.record(fresh)
        await peripheral.updateBalanceSnapshot(bytes)
        await repository.update(.balances, bytes: bytes)
        let summary = fresh.providers.map { "\($0.name)=\($0.status)" }.joined(separator: " ")
        FileHandle.standardOutput.write(Data("balance poll: \(summary)\n".utf8))
    }
}

extension BridgeService: GATTPeripheralDelegate {
    public nonisolated func gattPeripheral(_ peripheral: GATTPeripheral, refreshRequestedFor scope: UInt8) async {
        await handleRefresh(scope: scope)
    }
}

private final class BridgeRefreshBridge: @unchecked Sendable {
    weak var service: BridgeService?

    func refresh(scope: UInt8) async {
        await service?.handleRefresh(scope: scope)
    }
}

enum BridgeServiceLogStream: Equatable, Sendable {
    case standardOutput
    case standardError
}

struct BridgeServiceLogMessage: Equatable, Sendable {
    var stream: BridgeServiceLogStream
    var text: String
}
