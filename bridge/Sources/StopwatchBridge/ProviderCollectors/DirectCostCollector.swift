import Foundation

enum DirectCostProvider: Sendable {
    case codex
    case claude

    var usageProvider: UsageProvider {
        switch self {
        case .codex: return .codex
        case .claude: return .claude
        }
    }

    var stopwatchProviderID: ProviderID {
        switch self {
        case .codex: return .codex
        case .claude: return .claude
        }
    }
}

public struct DirectCostCollector: Sendable {
    public init() {}

    public func fetchAll(now: Date = Date()) async -> NormalizedCost {
        var providers: [NormalizedCost.Provider] = []
        if let codex = try? await fetch(.codex, now: now) { providers.append(codex) }
        if let claude = try? await fetch(.claude, now: now) { providers.append(claude) }
        var flags: CostFlags = []
        if providers.isEmpty { flags.insert(.costUnavailable) }
        return NormalizedCost(capturedAt: now, flags: flags, providers: providers)
    }

    func fetch(_ provider: DirectCostProvider, now: Date) async throws -> NormalizedCost.Provider {
        let snapshot = try await loadSnapshot(provider: provider, now: now)
        return Self.normalizedProvider(provider: provider, snapshot: snapshot, now: now)
    }

    func loadSnapshot(provider: DirectCostProvider, now: Date) async throws -> CostUsageTokenSnapshot {
        try await CostUsageFetcher().loadTokenSnapshot(
            provider: provider.usageProvider,
            now: now,
            historyDays: Protocol.costHistoryDays,
            refreshPricingInBackground: false)
    }

    static func normalizedProvider(provider: DirectCostProvider, snapshot: CostUsageTokenSnapshot, now: Date) -> NormalizedCost.Provider {
        let history = CodexbarClient.alignDailyHistory(snapshot.daily.map { ($0.date, $0.costUSD ?? 0) },
                                                       anchor: now,
                                                       days: Protocol.costHistoryDays)
        let modelDays = snapshot.daily.map { day -> (date: String, models: [String: UInt64]) in
            var models: [String: UInt64] = [:]
            for breakdown in day.modelBreakdowns ?? [] {
                models[breakdown.modelName, default: 0] += UInt64(max(0, breakdown.totalTokens ?? 0))
            }
            if models.isEmpty {
                for model in day.modelsUsed ?? [] {
                    models[model, default: 0] += UInt64(max(0, day.totalTokens ?? 0))
                }
            }
            return (date: day.date, models: models)
        }
        return .init(
            providerID: provider.stopwatchProviderID,
            todayCostUSD: snapshot.sessionCostUSD,
            monthCostUSD: snapshot.last30DaysCostUSD,
            todayTokens: snapshot.sessionTokens.map { UInt64(max(0, $0)) },
            monthTokens: snapshot.last30DaysTokens.map { UInt64(max(0, $0)) },
            models: CodexbarClient.latestDayModelsByTokens(from: modelDays),
            history: history)
    }
}
