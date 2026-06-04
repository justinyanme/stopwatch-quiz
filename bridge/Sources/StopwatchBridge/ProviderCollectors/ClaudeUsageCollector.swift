import Foundation

public struct ClaudeUsageCollector: Sendable {
    public var session: URLSession
    public var credentialsPath: URL

    public init(session: URLSession = .shared, credentialsPath: URL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".claude/.credentials.json")) {
        self.session = session
        self.credentialsPath = credentialsPath
    }

    public static func decodeOAuthUsage(_ data: Data) throws -> NormalizedUsage.Provider {
        let raw = try JSONDecoder().decode(RawOAuthUsage.self, from: data)
        return .init(
            providerID: .claude,
            status: .ok,
            // `utilization` is already a 0-100 percent on the live response.
            sessionPct: raw.fiveHour?.utilization.map(percentByte),
            weekPct: raw.sevenDay?.utilization.map(percentByte),
            sessionResetAt: CollectorDate.parseISO8601(raw.fiveHour?.resetsAt),
            weekResetAt: CollectorDate.parseISO8601(raw.sevenDay?.resetsAt),
            credits: nil,
            plan: ProviderPlan(fromString: raw.subscriptionType)
        )
    }

    public func fetch() async throws -> NormalizedUsage.Provider {
        let token = try Self.loadAccessToken(credentialsPath: credentialsPath)
        var req = URLRequest(url: URL(string: "https://api.anthropic.com/api/oauth/usage")!)
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        req.setValue("application/json", forHTTPHeaderField: "Accept")
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.setValue("oauth-2025-04-20", forHTTPHeaderField: "anthropic-beta")
        // Anthropic's OAuth usage endpoint expects a claude-code client User-Agent.
        req.setValue("claude-code/2.1.0", forHTTPHeaderField: "User-Agent")
        req.timeoutInterval = 20
        let (data, response) = try await session.data(for: req)
        let status = (response as? HTTPURLResponse)?.statusCode ?? -1
        guard status == 200 else {
            throw (status == 401 || status == 403) ? DirectCollectorError.auth : DirectCollectorError.http(status)
        }
        return try Self.decodeOAuthUsage(data)
    }

    static func loadAccessToken(credentialsPath: URL) throws -> String {
        let data = try Data(contentsOf: credentialsPath)
        let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any]
        // Current Claude credentials nest the token under "claudeAiOauth".
        if let oauth = obj?["claudeAiOauth"] as? [String: Any],
           let token = (oauth["accessToken"] ?? oauth["access_token"]) as? String {
            return token
        }
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
            var utilization: Double?
            var resetsAt: String?
            enum CodingKeys: String, CodingKey {
                case utilization
                case resetsAt = "resets_at"
            }
        }
    }
}
