import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct ClaudeUsageCollectorTests {
    @Test func mapsOAuthUsageToNormalizedProvider() throws {
        let data = Data("""
        {
          "five_hour": {"used_percent": 21, "resets_at": "2026-06-02T12:00:00Z"},
          "seven_day": {"used_percent": 65, "resets_at": "2026-06-08T12:00:00Z"},
          "subscription_type": "pro"
        }
        """.utf8)
        let provider = try ClaudeUsageCollector.decodeOAuthUsage(data)
        #expect(provider.providerID == .claude)
        #expect(provider.sessionPct == 21)
        #expect(provider.weekPct == 65)
        #expect(provider.plan == .pro)
    }
}
