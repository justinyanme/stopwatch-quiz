import Foundation

public struct ClaudeUsageCollector: Sendable {
    public var session: URLSession
    public var credentialsPath: URL

    public init(session: URLSession = .shared, credentialsPath: URL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".claude/.credentials.json")) {
        self.session = session
        self.credentialsPath = credentialsPath
    }

    public static func decodeOAuthUsage(_ data: Data) throws -> NormalizedUsage.Provider {
        let raw = try JSONDecoder.iso8601.decode(RawOAuthUsage.self, from: data)
        return .init(
            providerID: .claude,
            status: .ok,
            sessionPct: raw.fiveHour?.usedPercent.map(percentByte),
            weekPct: raw.sevenDay?.usedPercent.map(percentByte),
            sessionResetAt: raw.fiveHour?.resetsAt,
            weekResetAt: raw.sevenDay?.resetsAt,
            credits: nil,
            plan: ProviderPlan(fromString: raw.subscriptionType)
        )
    }

    public func fetch() async throws -> NormalizedUsage.Provider {
        let token = try Self.loadAccessToken(credentialsPath: credentialsPath)
        var req = URLRequest(url: URL(string: "https://api.anthropic.com/api/oauth/usage")!)
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        req.setValue("oauth-2025-04-20", forHTTPHeaderField: "anthropic-beta")
        req.timeoutInterval = 20
        let (data, response) = try await session.data(for: req)
        if let http = response as? HTTPURLResponse, http.statusCode == 401 || http.statusCode == 403 {
            throw DirectCollectorError.auth
        }
        return try Self.decodeOAuthUsage(data)
    }

    static func loadAccessToken(credentialsPath: URL) throws -> String {
        let data = try Data(contentsOf: credentialsPath)
        let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any]
        if let token = obj?["access_token"] as? String { return token }
        if let token = obj?["accessToken"] as? String { return token }
        throw DirectCollectorError.auth
    }

    private static func percentByte(_ value: Double) -> UInt8 {
        UInt8(max(0, min(100, value.rounded())))
    }

    private struct RawOAuthUsage: Decodable {
        var fiveHour: Window?
        var sevenDay: Window?
        var subscriptionType: String?
        enum CodingKeys: String, CodingKey {
            case fiveHour = "five_hour"
            case sevenDay = "seven_day"
            case subscriptionType = "subscription_type"
        }
        struct Window: Decodable {
            var usedPercent: Double?
            var resetsAt: Date?
            enum CodingKeys: String, CodingKey {
                case usedPercent = "used_percent"
                case resetsAt = "resets_at"
            }
        }
    }
}
