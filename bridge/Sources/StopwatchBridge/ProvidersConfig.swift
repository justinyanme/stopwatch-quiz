// bridge/Sources/StopwatchBridge/ProvidersConfig.swift
import Foundation

/// One entry of `providers.json`. Sparse on disk; `resolved()` fills `kind` defaults.
public struct ProviderConfig: Codable, Equatable, Sendable {
    public var id: String
    public var name: String
    public var kind: String?
    public var endpoint: String?
    public var auth: String?          // "bearer" (default) | "none" | "raw" (token verbatim, no scheme)
    public var balancePath: String?
    public var usagePath: String?
    public var currency: String?      // literal "USD" or "path:<dotted>"
    public var currencyDecimals: Int?
    public var pollSeconds: Int?
    public var lowThreshold: Double?
    public var scale: Double?         // raw balance/usage ÷ this = currency amount (e.g. 500000 for AiHubMix quota→USD)

    public struct Resolved: Equatable, Sendable {
        public var id: String, name: String
        public var kind: BalanceKind
        public var endpoint: String, auth: String
        public var balancePath: String, usagePath: String?
        public var currency: String, currencyDecimals: Int
        public var pollSeconds: Int, lowThreshold: Double?
        public var scale: Double
    }

    public func resolved() -> Resolved {
        let k = BalanceKind(fromString: kind)
        let d = Self.defaults(for: k)
        return Resolved(
            id: id, name: name, kind: k,
            endpoint: endpoint ?? d.endpoint,
            auth: auth ?? d.auth,
            balancePath: balancePath ?? d.balancePath,
            usagePath: usagePath ?? d.usagePath,
            currency: currency ?? d.currency,
            currencyDecimals: currencyDecimals ?? 2,
            pollSeconds: pollSeconds ?? 900,
            lowThreshold: lowThreshold,
            scale: scale ?? 1
        )
    }

    struct Defaults { var endpoint, auth, balancePath, currency: String; var usagePath: String? }

    static func defaults(for kind: BalanceKind) -> Defaults {
        switch kind {
        case .openrouter:
            return .init(endpoint: "https://openrouter.ai/api/v1/credits", auth: "bearer",
                         balancePath: "data.total_credits", currency: "USD", usagePath: "data.total_usage")
        case .deepseek:
            return .init(endpoint: "https://api.deepseek.com/user/balance", auth: "bearer",
                         balancePath: "balance_infos[0].total_balance",
                         currency: "path:balance_infos[0].currency", usagePath: nil)
        default:
            return .init(endpoint: "", auth: "bearer", balancePath: "", currency: "USD", usagePath: nil)
        }
    }
}

public enum ProvidersConfig {
    public static var defaultPath: URL {
        Config.defaultPath.deletingLastPathComponent().appendingPathComponent("providers.json")
    }

    public static func decode(_ data: Data) throws -> [ProviderConfig] {
        try JSONDecoder().decode([ProviderConfig].self, from: data)
    }

    public static func load(from url: URL = defaultPath) throws -> [ProviderConfig] {
        guard FileManager.default.fileExists(atPath: url.path) else { return [] }
        return try decode(Data(contentsOf: url))
    }
}
