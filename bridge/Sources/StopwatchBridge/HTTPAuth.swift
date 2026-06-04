public struct HTTPAuthenticator: Sendable {
    private let apiToken: String

    public init(apiToken: String) {
        self.apiToken = apiToken
    }

    public func isAuthorized(headers: [String: String]) -> Bool {
        guard !apiToken.isEmpty else { return false }
        guard let raw = headers["authorization"] else { return false }
        return raw == "Bearer \(apiToken)"
    }
}
