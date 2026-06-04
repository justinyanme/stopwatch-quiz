import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct GeminiUsageCollectorTests {
    // Live cloudcode-pa retrieveUserQuota shape uses "buckets".
    @Test func mapsQuotaBucketsToNormalizedProvider() throws {
        let data = Data("""
        {
          "buckets": [
            {"modelId": "gemini-2.5-pro", "remainingFraction": 0.42, "resetTime": "2026-06-02T12:00:00Z"},
            {"modelId": "gemini-2.5-flash", "remainingFraction": 0.88, "resetTime": "2026-06-03T12:00:00Z"}
          ]
        }
        """.utf8)
        let provider = try GeminiUsageCollector.decodeQuotaResponse(data)
        #expect(provider.providerID == .gemini)
        #expect(provider.sessionPct == 58)   // pro: (1 - 0.42) * 100
        #expect(provider.weekPct == 12)       // flash: (1 - 0.88) * 100
    }

    // Regression: Google quota resetTime can carry sub-second precision; it must not
    // fail the decode and drop the provider.
    @Test func fractionalResetTimeDoesNotDropProvider() throws {
        let data = Data("""
        {
          "buckets": [
            {"modelId": "gemini-2.5-pro", "remainingFraction": 0.42, "resetTime": "2026-06-02T12:00:00.123456789Z"}
          ]
        }
        """.utf8)
        let provider = try GeminiUsageCollector.decodeQuotaResponse(data)
        #expect(provider.sessionPct == 58)
        #expect(provider.sessionResetAt != nil)
    }
}
