import Foundation

public struct CodexUsageCollector: Sendable {
    public var session: URLSession
    public var authPath: URL

    public init(session: URLSession = .shared, authPath: URL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".codex/auth.json")) {
        self.session = session
        self.authPath = authPath
    }

    public static func decodeUsageResponse(_ data: Data, now: Date) throws -> NormalizedUsage.Provider {
        let raw = try JSONDecoder.iso8601.decode(RawWhamUsage.self, from: data)
        return .init(
            providerID: .codex,
            status: .ok,
            sessionPct: raw.rateLimit?.primaryWindow?.usedPercent.map(percentByte),
            weekPct: raw.rateLimit?.secondaryWindow?.usedPercent.map(percentByte),
            sessionResetAt: raw.rateLimit?.primaryWindow?.resetsAt,
            weekResetAt: raw.rateLimit?.secondaryWindow?.resetsAt,
            credits: raw.credits?.balance,
            plan: ProviderPlan(fromString: raw.account?.planType)
        )
    }

    public func fetch(now: Date = Date()) async throws -> NormalizedUsage.Provider {
        let token = try Self.loadAccessToken(authPath: authPath)
        var req = URLRequest(url: URL(string: "https://chatgpt.com/backend-api/wham/usage")!)
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        req.timeoutInterval = 20
        let (data, response) = try await session.data(for: req)
        if let http = response as? HTTPURLResponse, http.statusCode == 401 || http.statusCode == 403 {
            throw DirectCollectorError.auth
        }
        return try Self.decodeUsageResponse(data, now: now)
    }

    static func loadAccessToken(authPath: URL) throws -> String {
        let data = try Data(contentsOf: authPath)
        let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any]
        if let tokens = obj?["tokens"] as? [String: Any],
           let access = tokens["access_token"] as? String ?? tokens["accessToken"] as? String {
            return access
        }
        throw DirectCollectorError.auth
    }

    private static func percentByte(_ value: Double) -> UInt8 {
        UInt8(max(0, min(100, value.rounded())))
    }

    private struct RawWhamUsage: Decodable {
        var rateLimit: RateLimit?
        var credits: Credits?
        var account: Account?
        enum CodingKeys: String, CodingKey { case rateLimit = "rate_limit", credits, account }
        struct RateLimit: Decodable {
            var primaryWindow: Window?
            var secondaryWindow: Window?
            enum CodingKeys: String, CodingKey { case primaryWindow = "primary_window", secondaryWindow = "secondary_window" }
        }
        struct Window: Decodable {
            var usedPercent: Double?
            var resetsAt: Date?
            enum CodingKeys: String, CodingKey { case usedPercent = "used_percent", resetsAt = "resets_at" }
        }
        struct Credits: Decodable { var balance: Double? }
        struct Account: Decodable {
            var planType: String?
            enum CodingKeys: String, CodingKey { case planType = "plan_type" }
        }
    }
}

public enum DirectCollectorError: Error, Sendable {
    case auth
    case unavailable
}
