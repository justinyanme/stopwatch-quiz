import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct CostEncoderTests {

    @Test func encodesHeaderRecordsAndSharedScale() {
        let bytes = CostEncoder.encode(.costFixtureTwo)

        // Size = 12 + 85*2
        #expect(bytes.count == 182)
        // Header
        #expect(bytes[0] == Protocol.costVersionMajor)   // 2
        #expect(bytes[1] == Protocol.costVersionMinor)   // 0
        #expect(bytes[2] == 2)                            // recordCount
        #expect(bytes[3] == 0)                            // flags
        #expect(bytes[8] == 30)                           // historyDays
        #expect(bytes[9] == 0)                            // reserved
        #expect(bytes[10] == 48 && bytes[11] == 0)        // historyUnitCents = 48

        // Record 0 = codex at offset 12
        #expect(bytes[12] == ProviderID.codex.rawValue)
        #expect(bytes[14] == 0xB0 && bytes[15] == 0x04)   // todayCents 1200
        #expect(bytes[18] == 0x30 && bytes[19] == 0x75)   // monthCents 30000
        #expect(bytes[22] == 0x40 && bytes[23] == 0x42 && bytes[24] == 0x0F && bytes[25] == 0x00) // 1_000_000
        #expect(bytes[26] == 0x00 && bytes[27] == 0xE1 && bytes[28] == 0xF5 && bytes[29] == 0x05) // 100_000_000
        #expect(bytes[30] == 1)                           // modelCount
        #expect(Array(bytes[31..<38]) == Array("gpt-5.5".utf8))   // models[0]
        #expect(bytes[38] == 0)                           // null pad
        #expect(bytes[43..<67].allSatisfy { $0 == 0 })    // models[1] + models[2] empty
        // history[29] at 12+55+29 = 96 ⇒ 12000/48 = 250; everything before it is 0
        #expect(bytes[96] == 250)
        #expect(bytes[67..<96].allSatisfy { $0 == 0 })

        // Record 1 = claude at offset 97
        #expect(bytes[97] == ProviderID.claude.rawValue)
        #expect(bytes[115] == 3)                          // modelCount
        #expect(Array(bytes[116..<124]) == Array("opus-4-8".utf8))     // shortened models[0]
        #expect(Array(bytes[128..<138]) == Array("sonnet-4-6".utf8))   // models[1]
        #expect(Array(bytes[140..<149]) == Array("haiku-4-5".utf8))    // models[2]
        #expect(bytes[181] == 125)                        // claude history[29] (97+55+29) = 6000/48
    }

    @Test func costFixtureMatchesSavedHex() throws {
        let data = CostEncoder.encode(.costFixtureTwo)
        let url = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("shared/fixtures/codexbar-cost-two.hex")
        if ProcessInfo.processInfo.environment["FREEZE_FIXTURES"] != nil {
            let hex = data.map { String(format: "%02x", $0) }.joined()
            try (hex + "\n").write(to: url, atomically: true, encoding: .utf8)
        }
        let expected = try Fixtures.loadHex("codexbar-cost-two")
        #expect(data == expected)
    }

    @Test func unknownsEncodeAsSentinels() {
        let cost = NormalizedCost(
            capturedAt: Date(timeIntervalSince1970: 0), flags: [.stale],
            providers: [.init(providerID: .codex, todayCostUSD: nil, monthCostUSD: nil,
                              todayTokens: nil, monthTokens: nil, models: [],
                              history: [Double](repeating: 0, count: 30))])
        let bytes = CostEncoder.encode(cost)
        #expect(bytes[3] == CostFlags.stale.rawValue)
        // todayCents..monthTokens all 0xFFFFFFFF
        #expect(bytes[14..<30].allSatisfy { $0 == 0xFF })
        // modelCount 0, model slots all-zero
        #expect(bytes[30] == 0)
        #expect(bytes[31..<67].allSatisfy { $0 == 0 })
    }

    @Test func costCacheKeepsLastGoodOnFailure() {
        var cache = CostCache()
        let good = cache.recordSuccess(.costFixtureTwo)
        #expect(good[3] == 0)  // no flags

        let failed = cache.recordFailure(capturedAt: Date(timeIntervalSince1970: 1_748_500_000))
        // last-good record bytes preserved, stale+bridgeError set
        #expect(Array(failed[Protocol.costHeaderSize...]) == Array(good[Protocol.costHeaderSize...]))
        #expect((failed[3] & CostFlags.stale.rawValue) != 0)
        #expect((failed[3] & CostFlags.bridgeError.rawValue) != 0)
    }

    @Test func costCacheErrorEmptyBeforeFirstSuccess() {
        var cache = CostCache()
        let failed = cache.recordFailure()
        #expect(failed.count == Protocol.costHeaderSize)  // recordCount 0
        #expect((failed[3] & CostFlags.bridgeError.rawValue) != 0)
    }

    @Test func sharedScaleDrivenByLargerProvider() {
        var codexHist = [Double](repeating: 0, count: 30); codexHist[29] = 50.0    // $50
        var claudeHist = [Double](repeating: 0, count: 30); claudeHist[29] = 255.0 // $255 (larger)
        let cost = NormalizedCost(capturedAt: Date(timeIntervalSince1970: 0), flags: [], providers: [
            .init(providerID: .codex,  todayCostUSD: 0, monthCostUSD: 0, todayTokens: 0, monthTokens: 0, models: [], history: codexHist),
            .init(providerID: .claude, todayCostUSD: 0, monthCostUSD: 0, todayTokens: 0, monthTokens: 0, models: [], history: claudeHist),
        ])
        let bytes = CostEncoder.encode(cost)
        // max day = $255 = 25500 cents ⇒ historyUnitCents = ceil(25500/255) = 100
        #expect(bytes[10] == 100 && bytes[11] == 0)
        #expect(bytes[96] == 50)     // codex history[29] (offset 12+55+29) = 5000/100
        #expect(bytes[181] == 255)   // claude history[29] (offset 97+55+29) = 25500/100
    }

    @Test func costCacheEmptySuccessKeepsLastGoodWithCostUnavailable() {
        var cache = CostCache()
        let good = cache.recordSuccess(.costFixtureTwo)
        let empty = NormalizedCost(capturedAt: Date(timeIntervalSince1970: 1_748_500_000),
                                   flags: [.costUnavailable], providers: [])
        let out = cache.recordSuccess(empty)
        // last-known records preserved
        #expect(Array(out[Protocol.costHeaderSize...]) == Array(good[Protocol.costHeaderSize...]))
        #expect((out[3] & CostFlags.stale.rawValue) != 0)
        #expect((out[3] & CostFlags.costUnavailable.rawValue) != 0)
        #expect((out[3] & CostFlags.bridgeError.rawValue) == 0)   // NOT a bridge error
    }
}
