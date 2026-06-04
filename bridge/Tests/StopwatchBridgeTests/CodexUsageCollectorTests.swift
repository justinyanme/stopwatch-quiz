import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct CodexUsageCollectorTests {
    // Mirrors the live chatgpt.com/backend-api/wham/usage shape: top-level plan_type,
    // window used_percent, and reset_at as a unix timestamp.
    @Test func mapsWhamUsageToNormalizedProvider() throws {
        let data = Data("""
        {
          "plan_type": "pro",
          "rate_limit": {
            "primary_window": {"used_percent": 30, "reset_at": 1780491876},
            "secondary_window": {"used_percent": 59, "reset_at": 1780900000}
          },
          "credits": {"balance": 112.4}
        }
        """.utf8)
        let provider = try CodexUsageCollector.decodeUsageResponse(data, now: Date(timeIntervalSince1970: 1_780_000_000))
        #expect(provider.providerID == .codex)
        #expect(provider.sessionPct == 30)
        #expect(provider.weekPct == 59)
        #expect(provider.credits == 112.4)
        #expect(provider.plan == .pro)
        #expect(provider.sessionResetAt != nil)   // reset_at unix int parsed
    }

    @Test func loadsTokenAndAccountId() throws {
        let dir = FileManager.default.temporaryDirectory
        let url = dir.appendingPathComponent("codex-auth-\(UUID().uuidString).json")
        try Data(#"{"tokens":{"access_token":"tok-abc","account_id":"acc-123"}}"#.utf8).write(to: url)
        defer { try? FileManager.default.removeItem(at: url) }
        let auth = try CodexUsageCollector.loadAuth(authPath: url)
        #expect(auth.token == "tok-abc")
        #expect(auth.accountId == "acc-123")
    }
}
