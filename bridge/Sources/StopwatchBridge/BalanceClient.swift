// bridge/Sources/StopwatchBridge/BalanceClient.swift
import Foundation

/// Fetches prepaid balances directly from provider consoles. One GET per
/// provider, key resolved from a `KeyStore`, normalized via config selectors.
public actor BalanceClient {
    private let keyStore: KeyStore
    private let session: URLSession
    private let timeout: TimeInterval

    public init(keyStore: KeyStore, session: URLSession = .shared, timeout: TimeInterval = 20) {
        self.keyStore = keyStore; self.session = session; self.timeout = timeout
    }

    /// Fetches every provider concurrently, preserving input order. Per-provider
    /// failures become a record with the matching status (never throws).
    public func fetchAll(_ providers: [ProviderConfig.Resolved], now: Date = Date()) async -> NormalizedBalance {
        let results: [(Int, NormalizedBalance.Provider)] = await withTaskGroup(of: (Int, NormalizedBalance.Provider).self) { group in
            for (i, p) in providers.enumerated() {
                group.addTask { (i, await self.fetchOne(p, now: now)) }
            }
            var acc: [(Int, NormalizedBalance.Provider)] = []
            for await r in group { acc.append(r) }
            return acc
        }
        let ordered = results.sorted { $0.0 < $1.0 }.map { $0.1 }
        return NormalizedBalance(capturedAt: now, flags: [], providers: ordered)
    }

    private func fetchOne(_ p: ProviderConfig.Resolved, now: Date) async -> NormalizedBalance.Provider {
        func record(status: BalanceStatus, remaining: Double? = nil, unlimited: Bool = false,
                    usage: Double? = nil, currency: String = "", updatedAt: Date? = nil, isLow: Bool = false)
        -> NormalizedBalance.Provider {
            .init(kind: p.kind, name: p.name, status: status, currencyCode: currency,
                  currencyDecimals: p.currencyDecimals, remaining: remaining, unlimited: unlimited,
                  usage: usage, updatedAt: updatedAt, isLow: isLow)
        }

        // 1. Resolve key (unless keyless).
        var key: String? = nil
        if p.auth != "none" {
            guard let k = keyStore.key(for: p.id) else { return record(status: .authError) }
            key = k
        }

        // 2. Fetch (OpenRouter falls back /credits → /key).
        let attempts = openRouterFallback(p)
        var lastStatus: BalanceStatus = .unreachable
        for attempt in attempts {
            switch await get(attempt.endpoint, key: key) {
            case .failure(let s):
                lastStatus = s
            case .success(let obj):
                // Handle unlimited sentinel: when unlimitedIfNull and the balance path is null/missing.
                if attempt.unlimitedIfNull && rawIsNull(attempt.balancePath, in: obj) {
                    let currency = resolveCurrency(attempt.currency, in: obj)
                    return record(status: .ok, remaining: nil, unlimited: true,
                                  usage: nil, currency: currency, updatedAt: now, isLow: false)
                }
                guard let bal = numberAt(attempt.balancePath, in: obj) else {
                    lastStatus = .unreachable; continue
                }
                let usage = attempt.usagePath.flatMap { numberAt($0, in: obj) }
                let remaining = usage.map { bal - $0 } ?? bal
                let currency = resolveCurrency(attempt.currency, in: obj)
                let low = p.lowThreshold.map { remaining < $0 } ?? false
                return record(status: .ok, remaining: remaining, unlimited: false,
                              usage: usage, currency: currency, updatedAt: now, isLow: low)
            }
        }
        return record(status: lastStatus)
    }

    // Endpoint attempt with its own selectors (so the /key fallback can differ).
    private struct Attempt {
        var endpoint: String
        var balancePath: String
        var usagePath: String?
        var currency: String
        var unlimitedIfNull: Bool
    }

    /// Non-OpenRouter providers get a single attempt. OpenRouter tries `/credits`
    /// then falls back to `/key` (`limit_remaining`, null → unlimited). The fallback
    /// fires on ANY non-OK `/credits` result (including 401); the surfaced status
    /// still reflects the last failure, so a bad key correctly ends as `.authError`.
    private func openRouterFallback(_ p: ProviderConfig.Resolved) -> [Attempt] {
        var first = Attempt(endpoint: p.endpoint, balancePath: p.balancePath,
                            usagePath: p.usagePath, currency: p.currency, unlimitedIfNull: false)
        guard p.kind == .openrouter else { return [first] }
        let keyEndpoint = Attempt(endpoint: "https://openrouter.ai/api/v1/key",
                                  balancePath: "data.limit_remaining", usagePath: nil,
                                  currency: "USD", unlimitedIfNull: true)
        first.currency = "USD"   // OpenRouter credits are always USD; ignore any configured currency.
        return [first, keyEndpoint]
    }

    private enum GetResult { case success(Any); case failure(BalanceStatus) }

    private func get(_ endpoint: String, key: String?) async -> GetResult {
        guard let url = URL(string: endpoint) else { return .failure(.unreachable) }
        var req = URLRequest(url: url); req.timeoutInterval = timeout
        if let key { req.setValue("Bearer \(key)", forHTTPHeaderField: "Authorization") }
        req.setValue("application/json", forHTTPHeaderField: "Accept")
        do {
            let (data, resp) = try await session.data(for: req)
            if let http = resp as? HTTPURLResponse {
                switch http.statusCode {
                case 200: break
                case 401, 403: return .failure(.authError)
                case 402:      return .failure(.depleted)
                default:       return .failure(.unreachable)
                }
            }
            let obj = try JSONSerialization.jsonObject(with: data)
            return .success(obj)
        } catch {
            // Any transport error → unreachable. Unlike CodexbarClient we don't special-case
            // cancellation: balances are timer-polled, not trigger-cancelled, so there's no
            // superseding-fetch race to misreport.
            return .failure(.unreachable)
        }
    }

    // MARK: - dotted/indexed JSON path

    /// Evaluates `a.b[0].c` against a JSONSerialization object graph.
    public static func value(at path: String, in root: Any) -> Any? {
        var current: Any? = root
        for rawComponent in path.split(separator: ".") {
            var comp = String(rawComponent)
            var indices: [Int] = []
            while let open = comp.lastIndex(of: "["), comp.hasSuffix("]") {
                let idxStr = comp[comp.index(after: open)..<comp.index(before: comp.endIndex)]
                guard let n = Int(idxStr) else { break }
                indices.insert(n, at: 0)
                comp = String(comp[comp.startIndex..<open])
            }
            if !comp.isEmpty {
                guard let dict = current as? [String: Any] else { return nil }
                current = dict[comp]
            }
            for n in indices {
                guard let arr = current as? [Any], n >= 0, n < arr.count else { return nil }
                current = arr[n]
            }
        }
        return current
    }

    private func numberAt(_ path: String, in root: Any) -> Double? {
        // Under JSONSerialization, numbers come back as NSNumber; check it first
        // before the bridged Double/Int cases to avoid ambiguous bridging.
        switch BalanceClient.value(at: path, in: root) {
        case let n as NSNumber: return n.doubleValue
        case let s as String:   return Double(s)
        default:                return nil
        }
    }

    private func rawIsNull(_ path: String, in root: Any) -> Bool {
        let v = BalanceClient.value(at: path, in: root)
        return v == nil || v is NSNull
    }

    private func resolveCurrency(_ spec: String, in root: Any) -> String {
        if spec.hasPrefix("path:") {
            let p = String(spec.dropFirst("path:".count))
            return (BalanceClient.value(at: p, in: root) as? String)?.uppercased() ?? ""
        }
        return spec.uppercased()
    }
}
