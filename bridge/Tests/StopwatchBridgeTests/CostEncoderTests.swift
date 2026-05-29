import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct CostEncoderTests {

    @Test func encodesHeaderRecordsAndSharedScale() {
        let bytes = CostEncoder.encode(.costFixtureTwo)

        // Size = 12 + 60*2
        #expect(bytes.count == 132)
        // Header
        #expect(bytes[0] == Protocol.costVersionMajor)
        #expect(bytes[1] == Protocol.costVersionMinor)
        #expect(bytes[2] == 2)                  // recordCount
        #expect(bytes[3] == 0)                  // flags
        #expect(bytes[8] == 30)                 // historyDays
        #expect(bytes[9] == 0)                  // reserved
        #expect(bytes[10] == 48 && bytes[11] == 0)  // historyUnitCents = 48

        // Record 0 = codex at offset 12
        #expect(bytes[12] == ProviderID.codex.rawValue)
        #expect(bytes[14] == 0xB0 && bytes[15] == 0x04)  // todayCents 1200
        #expect(bytes[18] == 0x30 && bytes[19] == 0x75)  // monthCents 30000
        #expect(bytes[22] == 0x40 && bytes[23] == 0x42 && bytes[24] == 0x0F && bytes[25] == 0x00) // 1_000_000
        #expect(bytes[26] == 0x00 && bytes[27] == 0xE1 && bytes[28] == 0xF5 && bytes[29] == 0x05) // 100_000_000
        // model "gpt-5.5" at offset 30, null-padded
        #expect(Array(bytes[30..<37]) == Array("gpt-5.5".utf8))
        #expect(bytes[37] == 0)
        // history[29] at 12+30+29 = 71 ⇒ 12000/48 = 250; everything before it is 0
        #expect(bytes[71] == 250)
        #expect(bytes[42..<71].allSatisfy { $0 == 0 })

        // Record 1 = claude at offset 72; model shortened to "opus-4-7"
        #expect(bytes[72] == ProviderID.claude.rawValue)
        #expect(Array(bytes[90..<98]) == Array("opus-4-7".utf8))
        #expect(bytes[131] == 125)               // 6000/48 = 125
    }

    @Test func costFixtureMatchesSavedHex() throws {
        let expected = try Fixtures.loadHex("codexbar-cost-two")
        #expect(CostEncoder.encode(.costFixtureTwo) == expected)
    }

    @Test func unknownsEncodeAsSentinels() {
        let cost = NormalizedCost(
            capturedAt: Date(timeIntervalSince1970: 0), flags: [.stale],
            providers: [.init(providerID: .codex, todayCostUSD: nil, monthCostUSD: nil,
                              todayTokens: nil, monthTokens: nil, topModel: nil,
                              history: [Double](repeating: 0, count: 30))])
        let bytes = CostEncoder.encode(cost)
        #expect(bytes[3] == CostFlags.stale.rawValue)
        // todayCents..monthTokens all 0xFFFFFFFF
        #expect(bytes[14..<30].allSatisfy { $0 == 0xFF })
        // topModel all-zero
        #expect(bytes[30..<42].allSatisfy { $0 == 0 })
    }
}
