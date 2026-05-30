// bridge/Sources/StopwatchBridge/UsageClient.swift
import Foundation

/// Fetches per-provider usage/spend time-series. Phase 1 implements OpenRouter
/// (`/api/v1/activity`, management key). AIHubMix/DeepSeek land in later phases
/// behind the same `fetchAll` interface.
public actor UsageClient {
    public struct Target: Sendable, Equatable {
        public var kind: BalanceKind
        public var credentialID: String     // KeyStore id for the management key / token / cookie
        public init(kind: BalanceKind, credentialID: String) {
            self.kind = kind; self.credentialID = credentialID
        }
    }

    private let keyStore: KeyStore
    private let session: URLSession
    private let timeout: TimeInterval
    public init(keyStore: KeyStore, session: URLSession = .shared, timeout: TimeInterval = 25) {
        self.keyStore = keyStore; self.session = session; self.timeout = timeout
    }

    public func fetchAll(_ targets: [Target], now: Date = Date()) async -> NormalizedUsageSpend {
        var providers: [NormalizedUsageSpend.Provider] = []
        for t in targets {
            switch t.kind {
            case .openrouter: providers.append(await fetchOpenRouter(t, now: now))
            default:          providers.append(unavailable(t.kind))   // other kinds: later phases
            }
        }
        return NormalizedUsageSpend(capturedAt: now, flags: [], providers: providers)
    }

    private func unavailable(_ kind: BalanceKind) -> NormalizedUsageSpend.Provider {
        .init(kind: kind, status: .unreachable, currencyCode: "USD",
              todayCost: nil, monthCost: nil, todayTokens: nil, monthTokens: nil,
              todayRequests: nil, monthRequests: nil,
              costHistory: [Double](repeating: 0, count: 30), tokenHistory: [UInt64](repeating: 0, count: 30))
    }

    private func fetchOpenRouter(_ t: Target, now: Date) async -> NormalizedUsageSpend.Provider {
        guard let key = keyStore.key(for: t.credentialID) else {
            var p = unavailable(.openrouter); p.status = .authError; return p
        }
        guard let url = URL(string: "https://openrouter.ai/api/v1/activity") else {
            return unavailable(.openrouter)
        }
        var req = URLRequest(url: url); req.timeoutInterval = timeout
        req.setValue("Bearer \(key)", forHTTPHeaderField: "Authorization")
        req.setValue("application/json", forHTTPHeaderField: "Accept")
        do {
            let (data, resp) = try await session.data(for: req)
            if let http = resp as? HTTPURLResponse, http.statusCode != 200 {
                var p = unavailable(.openrouter)
                p.status = (http.statusCode == 401 || http.statusCode == 403) ? .authError : .unreachable
                return p
            }
            guard let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let rows = obj["data"] as? [[String: Any]] else {
                return unavailable(.openrouter)
            }
            return aggregate(rows, now: now)
        } catch {
            return unavailable(.openrouter)
        }
    }

    /// Buckets activity rows into 30 daily slots ending at `now`'s UTC day (index 29).
    private func aggregate(_ rows: [[String: Any]], now: Date) -> NormalizedUsageSpend.Provider {
        var cost = [Double](repeating: 0, count: 30)
        var toks = [UInt64](repeating: 0, count: 30)
        var reqs = [UInt64](repeating: 0, count: 30)
        var cal = Calendar(identifier: .gregorian); cal.timeZone = TimeZone(identifier: "UTC")!
        let today = cal.startOfDay(for: now)
        let fmt = DateFormatter()
        fmt.calendar = cal; fmt.timeZone = cal.timeZone; fmt.dateFormat = "yyyy-MM-dd"

        for row in rows {
            guard let dateStr = row["date"] as? String, let d = activityDate(dateStr, formatter: fmt) else { continue }
            let dayStart = cal.startOfDay(for: d)
            guard let diff = cal.dateComponents([.day], from: dayStart, to: today).day else { continue }
            let idx = 29 - diff
            guard idx >= 0 && idx < 30 else { continue }
            cost[idx] += num(row["usage"])
            let pt = u64(num(row["prompt_tokens"])); let ct = u64(num(row["completion_tokens"]))
            let rt = u64(num(row["reasoning_tokens"]))
            toks[idx] += pt + ct + rt
            reqs[idx] += u64(num(row["requests"]))
        }
        return .init(kind: .openrouter, status: .ok, currencyCode: "USD", currencyDecimals: 2,
                     todayCost: cost[29], monthCost: cost.reduce(0,+),
                     todayTokens: toks[29], monthTokens: toks.reduce(0,+),
                     todayRequests: reqs[29], monthRequests: reqs.reduce(0,+),
                     costHistory: cost, tokenHistory: toks)
    }

    private func num(_ v: Any?) -> Double {
        switch v {
        case let n as NSNumber: return n.doubleValue
        case let s as String:   return Double(s) ?? 0
        default:                return 0
        }
    }

    private func activityDate(_ s: String, formatter: DateFormatter) -> Date? {
        guard s.count >= 10 else { return nil }
        return formatter.date(from: String(s.prefix(10)))
    }

    /// Safe Double→UInt64: clamps negatives, NaN, and infinities to 0 (a bare
    /// UInt64(Double) traps on those). Token/request counts are never negative.
    private func u64(_ v: Double) -> UInt64 {
        guard v.isFinite, v >= 0 else { return 0 }
        return UInt64(v)
    }
}
