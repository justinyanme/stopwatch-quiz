import Foundation

public struct GeminiUsageCollector: Sendable {
    public var session: URLSession
    public var credentialsPath: URL

    public init(session: URLSession = .shared, credentialsPath: URL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".gemini/oauth_creds.json")) {
        self.session = session
        self.credentialsPath = credentialsPath
    }

    public static func decodeQuotaResponse(_ data: Data) throws -> NormalizedUsage.Provider {
        let raw = try JSONDecoder.iso8601.decode(RawQuota.self, from: data)
        let pro = raw.quotaBuckets.filter { $0.modelId.lowercased().contains("pro") }
            .min { ($0.remainingFraction ?? 1) < ($1.remainingFraction ?? 1) }
        let flash = raw.quotaBuckets.filter { $0.modelId.lowercased().contains("flash") }
            .min { ($0.remainingFraction ?? 1) < ($1.remainingFraction ?? 1) }
        return .init(
            providerID: .gemini,
            status: .ok,
            sessionPct: pro?.remainingFraction.map { percentUsedFromRemaining($0) },
            weekPct: flash?.remainingFraction.map { percentUsedFromRemaining($0) },
            sessionResetAt: pro?.resetTime,
            weekResetAt: flash?.resetTime,
            credits: nil,
            plan: plan(from: raw.tier)
        )
    }

    public func fetch() async throws -> NormalizedUsage.Provider {
        let token = try Self.loadAccessToken(credentialsPath: credentialsPath)
        var req = URLRequest(url: URL(string: "https://cloudcode-pa.googleapis.com/v1internal:retrieveUserQuota")!)
        req.httpMethod = "POST"
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.httpBody = Data(#"{}"#.utf8)
        req.timeoutInterval = 20
        let (data, response) = try await session.data(for: req)
        if let http = response as? HTTPURLResponse, http.statusCode == 401 || http.statusCode == 403 {
            throw DirectCollectorError.auth
        }
        return try Self.decodeQuotaResponse(data)
    }

    static func loadAccessToken(credentialsPath: URL) throws -> String {
        let data = try Data(contentsOf: credentialsPath)
        let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any]
        guard let token = obj?["access_token"] as? String else { throw DirectCollectorError.auth }
        return token
    }

    private static func percentUsedFromRemaining(_ remaining: Double) -> UInt8 {
        UInt8(max(0, min(100, ((1 - remaining) * 100).rounded())))
    }

    private static func plan(from tier: String?) -> ProviderPlan {
        switch (tier ?? "").lowercased() {
        case "standard-tier": return .plus
        case "free-tier": return .free
        default: return .unknown
        }
    }

    private struct RawQuota: Decodable {
        var quotaBuckets: [Bucket]
        var tier: String?
        struct Bucket: Decodable {
            var modelId: String
            var remainingFraction: Double?
            var resetTime: Date?
        }
    }
}
