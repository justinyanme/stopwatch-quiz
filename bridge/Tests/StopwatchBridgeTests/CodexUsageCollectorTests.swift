import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct CodexUsageCollectorTests {
    @Test func mapsWhamUsageToNormalizedProvider() throws {
        let data = Data("""
        {
          "rate_limit": {
            "primary_window": {"used_percent": 28, "resets_at": "2026-06-02T12:00:00Z"},
            "secondary_window": {"used_percent": 59, "resets_at": "2026-06-06T12:00:00Z"}
          },
          "credits": {"balance": 112.4},
          "account": {"plan_type": "plus"}
        }
        """.utf8)
        let provider = try CodexUsageCollector.decodeUsageResponse(data, now: Date(timeIntervalSince1970: 1_780_000_000))
        #expect(provider.providerID == .codex)
        #expect(provider.sessionPct == 28)
        #expect(provider.weekPct == 59)
        #expect(provider.credits == 112.4)
        #expect(provider.plan == .plus)
    }
}
