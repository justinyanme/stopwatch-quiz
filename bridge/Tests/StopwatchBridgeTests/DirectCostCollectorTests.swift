import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct DirectCostCollectorTests {
    @Test func mapsTokenSnapshotToNormalizedCostProvider() {
        let token = CostUsageTokenSnapshot(
            sessionTokens: 100,
            sessionCostUSD: 1.25,
            last30DaysTokens: 1000,
            last30DaysCostUSD: 12.50,
            daily: [
                .init(
                    date: "2026-06-02",
                    inputTokens: 50,
                    outputTokens: 50,
                    totalTokens: 100,
                    costUSD: 1.25,
                    modelsUsed: ["gpt-5.5"],
                    modelBreakdowns: [
                        .init(modelName: "gpt-5.5", costUSD: 1.25, totalTokens: 100)
                    ])
            ],
            updatedAt: Date(timeIntervalSince1970: 1_780_000_000))

        let provider = DirectCostCollector.normalizedProvider(provider: .codex, snapshot: token, now: token.updatedAt)

        #expect(provider.providerID == .codex)
        #expect(provider.todayCostUSD == 1.25)
        #expect(provider.monthCostUSD == 12.50)
        #expect(provider.todayTokens == 100)
        #expect(provider.monthTokens == 1000)
        #expect(provider.models == ["gpt-5.5"])
    }
}
