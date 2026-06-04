import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct ClaudeUsageCollectorTests {
    // Live api.anthropic.com/api/oauth/usage shape: window field is `utilization` (0-100 percent).
    @Test func mapsOAuthUsageToNormalizedProvider() throws {
        let data = Data("""
        {
          "five_hour": {"utilization": 21, "resets_at": "2026-06-02T12:00:00Z"},
          "seven_day": {"utilization": 65, "resets_at": "2026-06-08T12:00:00Z"}
        }
        """.utf8)
        let provider = try ClaudeUsageCollector.decodeOAuthUsage(data)
        #expect(provider.providerID == .claude)
        #expect(provider.sessionPct == 21)
        #expect(provider.weekPct == 65)
        #expect(provider.sessionResetAt != nil)
    }

    // Regression: the token is nested under `claudeAiOauth`, not top-level.
    @Test func loadsNestedClaudeAiOauthToken() throws {
        let dir = FileManager.default.temporaryDirectory
        let url = dir.appendingPathComponent("claude-creds-\(UUID().uuidString).json")
        try Data(#"{"claudeAiOauth":{"accessToken":"nested-abc","refreshToken":"r"}}"#.utf8).write(to: url)
        defer { try? FileManager.default.removeItem(at: url) }
        let token = try ClaudeUsageCollector.loadAccessToken(credentialsPath: url)
        #expect(token == "nested-abc")
    }
}
