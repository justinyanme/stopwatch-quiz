import Foundation
import Testing
@testable import StopwatchBridge

// Pure helpers only — no URLProtocol stub here, so this suite can run in
// parallel with others safely. Stub-based fetchCost tests live in
// CodexbarClientTests (the serialized suite that owns StubURLProtocol).
@Suite struct CostClientTests {

    private func utcDate(_ ymd: String) -> Date {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.timeZone = TimeZone(identifier: "UTC")
        f.dateFormat = "yyyy-MM-dd"
        return f.date(from: ymd)!
    }

    @Test func alignsDailyHistoryByDateOffset() {
        let daily = [("2026-05-29", 120.0), ("2026-05-27", 10.0), ("2026-04-01", 999.0)]
        let hist = CodexbarClient.alignDailyHistory(daily, anchor: utcDate("2026-05-29"), days: 30)
        #expect(hist.count == 30)
        #expect(hist[29] == 120.0)   // today
        #expect(hist[27] == 10.0)    // 2 days ago
        #expect(hist[0...26].allSatisfy { $0 == 0 })
        #expect(hist[28] == 0)
        // 2026-04-01 is > 29 days back ⇒ dropped, not summed anywhere
        #expect(hist.reduce(0, +) == 130.0)
    }

    @Test func picksHighestCostModel() {
        let model = CodexbarClient.topModel(from: [
            ["gpt-5.5": 10.0, "gpt-4": 1.0],
            ["gpt-5.5": 5.0],
        ])
        #expect(model == "gpt-5.5")
    }
}
