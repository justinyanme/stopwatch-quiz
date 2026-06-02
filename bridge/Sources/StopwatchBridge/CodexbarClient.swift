// bridge/Sources/StopwatchBridge/CodexbarClient.swift
import Foundation

/// Thin client around `codexbar serve` localhost JSON. Produces `NormalizedUsage`
/// so `SnapshotEncoder` doesn't need to know about the CodexBar wire format.
public actor CodexbarClient {

    public enum Scope: String {
        case all = "all"
        case codex, claude, gemini
    }

    public enum FetchError: Error {
        case http(Int)
        case transport(Error)
        case decode(Error)
    }

    public init(port: UInt16, session: URLSession = .shared, timeout: TimeInterval = 30) {
        self.port = port
        self.session = session
        self.timeout = timeout
    }

    public func fetch(scope: Scope = .all) async throws -> NormalizedUsage {
        var components = URLComponents()
        components.scheme = "http"
        components.host   = "127.0.0.1"
        components.port   = Int(port)
        components.path   = "/usage"
        if scope != .all {
            components.queryItems = [.init(name: "provider", value: scope.rawValue)]
        }
        var req = URLRequest(url: components.url!)
        req.timeoutInterval = timeout

        let data: Data
        let response: URLResponse
        do {
            (data, response) = try await session.data(for: req)
        } catch {
            throw FetchError.transport(error)
        }
        if let http = response as? HTTPURLResponse, http.statusCode != 200 {
            throw FetchError.http(http.statusCode)
        }
        do {
            return try decode(data)
        } catch {
            throw FetchError.decode(error)
        }
    }

    // MARK: - Decoding the CodexBar response shape

    private func decode(_ data: Data) throws -> NormalizedUsage {
        // Real `codexbar serve` returns a top-level JSON ARRAY of provider objects.
        // Our test fixtures use a wrapped object. Try array first, fall back to wrapper.
        let rawProviders: [RawResponse.Provider]
        let rawFlags: RawResponse.Flags?
        let rawCapturedAt: Date?
        do {
            rawProviders = try JSONDecoder.iso8601.decode([RawResponse.Provider].self, from: data)
            rawFlags = nil
            rawCapturedAt = nil
        } catch {
            let wrapped = try JSONDecoder.iso8601.decode(RawResponse.self, from: data)
            rawProviders = wrapped.providers
            rawFlags = wrapped.flags
            rawCapturedAt = wrapped.capturedAt
        }

        var flagBits: SnapshotFlags = []
        if rawFlags?.stale       == true { flagBits.insert(.stale) }
        if rawFlags?.bridgeError == true { flagBits.insert(.bridgeError) }
        if rawProviders.isEmpty           { flagBits.insert(.providerMissing) }

        let providers = rawProviders.compactMap { p -> NormalizedUsage.Provider? in
            guard let id = ProviderID(fromString: p.provider) else { return nil }
            return .init(
                providerID:     id,
                status:         ProviderStatus(fromIndicator: p.status?.indicator),
                sessionPct:     p.usage?.primary?.usedPercent.map { UInt8(max(0, min($0.rounded(), 100))) },
                weekPct:        p.usage?.secondary?.usedPercent.map { UInt8(max(0, min($0.rounded(), 100))) },
                sessionResetAt: p.usage?.primary?.resetsAt,
                weekResetAt:    p.usage?.secondary?.resetsAt,
                credits:        p.credits?.remaining,
                plan:           ProviderPlan(fromString: p.plan)
            )
        }

        return .init(capturedAt: rawCapturedAt ?? Date(), flags: flagBits, providers: providers)
    }

    // MARK: - Wire shapes (subset of `codexbar serve` JSON we actually use)

    private struct RawResponse: Decodable {
        var capturedAt: Date?
        var flags: Flags?
        var providers: [Provider]

        struct Flags: Decodable { var stale, bridgeError: Bool? }

        struct Provider: Decodable {
            var provider: String
            var status: Status?
            var usage: Usage?
            var credits: Credits?
            var plan: String?

            struct Status: Decodable { var indicator: String? }
            struct Usage: Decodable {
                var primary: Window?
                var secondary: Window?
                // `usedPercent` is a Double on the wire: codexbar reports fractional
            // percentages (Gemini e.g. 54.666664999999995). Decoding as Int throws
            // and fails the entire array — see `decodesFractionalUsedPercent` test.
            struct Window: Decodable { var usedPercent: Double?; var resetsAt: Date? }
            }
            struct Credits: Decodable { var remaining: Double? }
        }
    }

    public func fetchCost(scope: Scope = .all, now: Date = Date()) async throws -> NormalizedCost {
        var components = URLComponents()
        components.scheme = "http"
        components.host   = "127.0.0.1"
        components.port   = Int(port)
        components.path   = "/cost"
        if scope != .all {
            components.queryItems = [.init(name: "provider", value: scope.rawValue)]
        }
        var req = URLRequest(url: components.url!)
        req.timeoutInterval = timeout

        let data: Data
        let response: URLResponse
        do {
            (data, response) = try await session.data(for: req)
        } catch {
            throw FetchError.transport(error)
        }
        if let http = response as? HTTPURLResponse, http.statusCode != 200 {
            throw FetchError.http(http.statusCode)
        }
        do {
            return try decodeCost(data, now: now)
        } catch {
            throw FetchError.decode(error)
        }
    }

    private func decodeCost(_ data: Data, now: Date) throws -> NormalizedCost {
        let raw = try JSONDecoder().decode([RawCost].self, from: data)

        let providers = raw.compactMap { c -> NormalizedCost.Provider? in
            guard let id = ProviderID(fromString: c.provider) else { return nil }
            let daily = c.daily ?? []
            let dailyPairs = daily.map { (date: $0.date, costUSD: $0.totalCost ?? 0) }
            let tokenDays = daily.map { day -> (date: String, models: [String: UInt64]) in
                var m: [String: UInt64] = [:]
                for b in day.modelBreakdowns ?? [] {
                    m[b.modelName, default: 0] += UInt64(max(0, (b.totalTokens ?? 0).rounded()))
                }
                return (date: day.date, models: m)
            }
            return .init(
                providerID:   id,
                todayCostUSD: c.sessionCostUSD,
                monthCostUSD: c.last30DaysCostUSD,
                todayTokens:  c.sessionTokens.map { UInt64(max(0, $0.rounded())) },
                monthTokens:  c.last30DaysTokens.map { UInt64(max(0, $0.rounded())) },
                models:       Self.latestDayModelsByTokens(from: tokenDays),
                history:      Self.alignDailyHistory(dailyPairs, anchor: now,
                                                     days: Protocol.costHistoryDays)
            )
        }

        var flags: CostFlags = []
        if providers.isEmpty { flags.insert(.costUnavailable) }
        return .init(capturedAt: now, flags: flags, providers: providers)
    }

    private struct RawCost: Decodable {
        var provider: String
        var sessionCostUSD: Double?
        var sessionTokens: Double?
        var last30DaysCostUSD: Double?
        var last30DaysTokens: Double?
        var daily: [Daily]?

        struct Daily: Decodable {
            var date: String
            var totalCost: Double?
            var modelBreakdowns: [ModelBreakdown]?
            struct ModelBreakdown: Decodable { var modelName: String; var cost: Double?; var totalTokens: Double? }
        }
    }

    /// Buckets sparse daily (date string, USD) into a dense `days`-length array,
    /// index `days-1` = anchor's UTC day, older days toward index 0. Out-of-window dropped.
    static func alignDailyHistory(_ daily: [(date: String, costUSD: Double)],
                                  anchor: Date, days: Int = 30) -> [Double] {
        var out = [Double](repeating: 0, count: days)
        var cal = Calendar(identifier: .gregorian)
        cal.timeZone = TimeZone(identifier: "UTC")!
        let today = cal.startOfDay(for: anchor)
        for entry in daily {
            guard let d = costDay(entry.date) else { continue }
            let offset = cal.dateComponents([.day], from: cal.startOfDay(for: d), to: today).day ?? -1
            if offset >= 0 && offset < days { out[days - 1 - offset] += entry.costUSD }
        }
        return out
    }

    /// Models used on the newest dated daily record, ordered by today's tokens
    /// (descending), ties broken by model name ascending. Empty if no dated day
    /// has model tokens. Token order surfaces the dominant model first — including
    /// one codexbar hasn't priced yet, since the breakdown carries `totalTokens`
    /// even when `cost` is null.
    static func latestDayModelsByTokens(from daily: [(date: String, models: [String: UInt64])]) -> [String] {
        var latestDay: Date?
        var latestModels: [String: UInt64] = [:]

        for day in daily {
            guard !day.models.isEmpty, let date = costDay(day.date) else { continue }
            if let currentLatest = latestDay {
                if date > currentLatest {
                    latestDay = date
                    latestModels = day.models
                } else if date == currentLatest {
                    for (name, tokens) in day.models { latestModels[name, default: 0] += tokens }
                }
            } else {
                latestDay = date
                latestModels = day.models
            }
        }

        return latestModels
            .sorted { a, b in a.value != b.value ? a.value > b.value : a.key < b.key }
            .map(\.key)
    }

    private static func costDay(_ value: String) -> Date? {
        let fmt = DateFormatter()
        fmt.locale = Locale(identifier: "en_US_POSIX")
        fmt.timeZone = TimeZone(identifier: "UTC")!
        fmt.dateFormat = "yyyy-MM-dd"
        return fmt.date(from: value)
    }

    /// True if `error` is intentional task cancellation (a newer trigger superseded this
    /// fetch), not a real failure. URLSession surfaces cancellation as `URLError(.cancelled)`,
    /// which `fetch`/`fetchCost` wrap in `FetchError.transport`.
    static func isCancellation(_ error: Error) -> Bool {
        if error is CancellationError { return true }
        if (error as? URLError)?.code == .cancelled { return true }
        if case let FetchError.transport(inner) = error,
           (inner as? URLError)?.code == .cancelled { return true }
        return false
    }

    private let port: UInt16
    private let session: URLSession
    private let timeout: TimeInterval
}

extension JSONDecoder {
    static let iso8601: JSONDecoder = {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        return d
    }()
}

private extension ProviderID {
    init?(fromString s: String) {
        switch s.lowercased() {
        case "codex":  self = .codex
        case "claude": self = .claude
        case "gemini": self = .gemini
        default:       return nil
        }
    }
}

private extension ProviderStatus {
    init(fromIndicator s: String?) {
        switch (s ?? "").lowercased() {
        case "none":     self = .ok
        case "minor":    self = .warn
        case "major":    self = .critical
        case "critical": self = .critical
        case "":         self = .ok
        default:         self = .ok
        }
    }
}
