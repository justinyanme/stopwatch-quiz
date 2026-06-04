import Foundation

public struct CodexUsageCollector: Sendable {
    public var session: URLSession
    public var authPath: URL

    public init(session: URLSession = .shared, authPath: URL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".codex/auth.json")) {
        self.session = session
        self.authPath = authPath
    }

    public static func decodeUsageResponse(_ data: Data, now: Date) throws -> NormalizedUsage.Provider {
        let raw = try JSONDecoder().decode(RawWhamUsage.self, from: data)
        func resetDate(_ unix: Int?) -> Date? { unix.map { Date(timeIntervalSince1970: Double($0)) } }
        return .init(
            providerID: .codex,
            status: .ok,
            sessionPct: raw.rateLimit?.primaryWindow?.usedPercent.map(percentByte),
            weekPct: raw.rateLimit?.secondaryWindow?.usedPercent.map(percentByte),
            sessionResetAt: resetDate(raw.rateLimit?.primaryWindow?.resetAt),
            weekResetAt: resetDate(raw.rateLimit?.secondaryWindow?.resetAt),
            credits: raw.credits?.balance,
            // `plan_type` is top-level on the live response; older shape nested it under `account`.
            plan: ProviderPlan(fromString: raw.planType ?? raw.account?.planType)
        )
    }

    public func fetch(now: Date = Date()) async throws -> NormalizedUsage.Provider {
        let auth = try Self.loadAuth(authPath: authPath)
        var req = URLRequest(url: URL(string: "https://chatgpt.com/backend-api/wham/usage")!)
        req.setValue("Bearer \(auth.token)", forHTTPHeaderField: "Authorization")
        // ChatGPT's backend rejects the default URLSession User-Agent; match CodexBar's request.
        req.setValue("CodexBar", forHTTPHeaderField: "User-Agent")
        req.setValue("application/json", forHTTPHeaderField: "Accept")
        if let accountId = auth.accountId, !accountId.isEmpty {
            req.setValue(accountId, forHTTPHeaderField: "ChatGPT-Account-Id")
        }
        req.timeoutInterval = 20
        let (data, response) = try await session.data(for: req)
        let status = (response as? HTTPURLResponse)?.statusCode ?? -1
        guard status == 200 else {
            throw (status == 401 || status == 403) ? DirectCollectorError.auth : DirectCollectorError.http(status)
        }
        return try Self.decodeUsageResponse(data, now: now)
    }

    static func loadAuth(authPath: URL) throws -> (token: String, accountId: String?) {
        let data = try Data(contentsOf: authPath)
        let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any]
        guard let tokens = obj?["tokens"] as? [String: Any],
              let access = tokens["access_token"] as? String ?? tokens["accessToken"] as? String else {
            throw DirectCollectorError.auth
        }
        let accountId = tokens["account_id"] as? String ?? tokens["accountId"] as? String
        return (access, accountId)
    }

    private static func percentByte(_ value: Double) -> UInt8 {
        UInt8(max(0, min(100, value.rounded())))
    }

    private struct RawWhamUsage: Decodable {
        var rateLimit: RateLimit?
        var credits: Credits?
        var account: Account?
        var planType: String?
        enum CodingKeys: String, CodingKey { case rateLimit = "rate_limit", credits, account, planType = "plan_type" }
        // Decode each field independently so one malformed/unexpected field
        // (e.g. a string-typed credits balance) never discards the rate-limit
        // percentages the rings actually need.
        init(from decoder: Decoder) throws {
            let c = try decoder.container(keyedBy: CodingKeys.self)
            rateLimit = try? c.decodeIfPresent(RateLimit.self, forKey: .rateLimit)
            credits = try? c.decodeIfPresent(Credits.self, forKey: .credits)
            account = try? c.decodeIfPresent(Account.self, forKey: .account)
            planType = try? c.decodeIfPresent(String.self, forKey: .planType)
        }
        struct RateLimit: Decodable {
            var primaryWindow: Window?
            var secondaryWindow: Window?
            enum CodingKeys: String, CodingKey { case primaryWindow = "primary_window", secondaryWindow = "secondary_window" }
        }
        struct Window: Decodable {
            var usedPercent: Double?
            var resetAt: Int?       // unix epoch seconds
            enum CodingKeys: String, CodingKey { case usedPercent = "used_percent", resetAt = "reset_at" }
        }
        // `balance` may arrive as a JSON number or a numeric string.
        struct Credits: Decodable {
            var balance: Double?
            enum CodingKeys: String, CodingKey { case balance }
            init(from decoder: Decoder) throws {
                let c = try decoder.container(keyedBy: CodingKeys.self)
                if let d = try? c.decode(Double.self, forKey: .balance) {
                    balance = d
                } else if let s = try? c.decode(String.self, forKey: .balance) {
                    balance = Double(s)
                } else {
                    balance = nil
                }
            }
        }
        struct Account: Decodable {
            var planType: String?
            enum CodingKeys: String, CodingKey { case planType = "plan_type" }
        }
    }
}

public enum DirectCollectorError: Error, Sendable {
    case auth
    case http(Int)
    case unavailable
}
