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
            let dailyPairs = (c.daily ?? []).map { (date: $0.date, costUSD: $0.totalCost ?? 0) }
            let breakdowns = (c.daily ?? []).map { day -> [String: Double] in
                var m: [String: Double] = [:]
                for b in day.modelBreakdowns ?? [] { m[b.modelName, default: 0] += b.cost ?? 0 }
                return m
            }
            return .init(
                providerID:   id,
                todayCostUSD: c.sessionCostUSD,
                monthCostUSD: c.last30DaysCostUSD,
                todayTokens:  c.sessionTokens.map { UInt64(max(0, $0.rounded())) },
                monthTokens:  c.last30DaysTokens.map { UInt64(max(0, $0.rounded())) },
                topModel:     Self.topModel(from: breakdowns),
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
            struct ModelBreakdown: Decodable { var modelName: String; var cost: Double? }
        }
    }

    /// Buckets sparse daily (date string, USD) into a dense `days`-length array,
    /// index `days-1` = anchor's UTC day, older days toward index 0. Out-of-window dropped.
    static func alignDailyHistory(_ daily: [(date: String, costUSD: Double)],
                                  anchor: Date, days: Int = 30) -> [Double] {
        var out = [Double](repeating: 0, count: days)
        var cal = Calendar(identifier: .gregorian)
        cal.timeZone = TimeZone(identifier: "UTC")!
        let fmt = DateFormatter()
        fmt.locale = Locale(identifier: "en_US_POSIX")
        fmt.timeZone = TimeZone(identifier: "UTC")!
        fmt.dateFormat = "yyyy-MM-dd"
        let today = cal.startOfDay(for: anchor)
        for entry in daily {
            guard let d = fmt.date(from: entry.date) else { continue }
            let offset = cal.dateComponents([.day], from: cal.startOfDay(for: d), to: today).day ?? -1
            if offset >= 0 && offset < days { out[days - 1 - offset] += entry.costUSD }
        }
        return out
    }

    /// Sums cost per model across daily breakdowns, returns the highest-cost model name.
    static func topModel(from breakdowns: [[String: Double]]) -> String? {
        var totals: [String: Double] = [:]
        for day in breakdowns {
            for (name, cost) in day { totals[name, default: 0] += cost }
        }
        return totals.max { $0.value < $1.value }?.key
    }

    private let port: UInt16
    private let session: URLSession
    private let timeout: TimeInterval
}

private extension JSONDecoder {
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
