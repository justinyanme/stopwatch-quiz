import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct UsageEncoderTests {

    private func fixture() -> NormalizedUsageSpend {
        var cost = [Double](repeating: 0, count: 30); cost[29] = 100.0   // $100 on the last day
        var tok  = [UInt64](repeating: 0, count: 30);  tok[29] = 1_000_000
        return .init(capturedAt: Date(timeIntervalSince1970: 1_748_455_822), flags: [], providers: [
            .init(kind: .openrouter, status: .ok, currencyCode: "USD", currencyDecimals: 2,
                  todayCost: 4.00, monthCost: 41.80, todayTokens: 1_000_000, monthTokens: 2_100_000,
                  todayRequests: 1240, monthRequests: 9000, costHistory: cost, tokenHistory: tok)
        ])
    }

    @Test func encodesHeaderAndRecord() {
        let bytes = [UInt8](UsageEncoder.encode(fixture()))
        #expect(bytes.count == 12 + 96)             // one record
        #expect(bytes[0] == Protocol.usageVersionMajor)
        #expect(bytes[2] == 1)                       // recordCount
        #expect(bytes[3] == 0)                       // flags
        #expect(bytes[8] == 30)                      // historyDays

        let r = 12
        #expect(bytes[r+0] == BalanceKind.openrouter.rawValue)
        #expect(bytes[r+1] == BalanceStatus.ok.rawValue)
        #expect(Array(bytes[(r+2)..<(r+5)]) == Array("USD".utf8))
        #expect(bytes[r+5] == 2)                     // decimals
        // todayCostMinor = 400 (4.00 * 100) little-endian at r+12
        #expect(bytes[r+12] == 0x90 && bytes[r+13] == 0x01)
        // costHistory[29]: max day = $100 = 10000 minor; costUnit = ceil(10000/255) = 40
        let costUnit = UInt16(bytes[r+6]) | (UInt16(bytes[r+7]) << 8)
        #expect(costUnit == 40)
        // encoder rounds: (10000 + 40/2) / 40 = 250
        #expect(bytes[r+36+29] == 250)
        // tokenHistory[29]: max = 1_000_000; tokenUnit = ceil(1_000_000/255) = 3922
        let tokenUnit = UInt32(bytes[r+8]) | (UInt32(bytes[r+9])<<8) | (UInt32(bytes[r+10])<<16) | (UInt32(bytes[r+11])<<24)
        #expect(tokenUnit == 3922)
        // encoder rounds: (1_000_000 + 3922/2) / 3922 = 255
        #expect(bytes[r+66+29] == 255)
    }

    @Test func writesAndMatchesOpenRouterFixture() throws {
        let bytes = UsageEncoder.encode(.openRouterFixture)
        // Regenerate the golden file when intentionally changing the format: REGEN=1.
        let url = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("shared/fixtures/usage-openrouter.hex")
        if ProcessInfo.processInfo.environment["REGEN"] == "1" {
            let hex = bytes.map { String(format: "%02x", $0) }.joined()
            try hex.write(to: url, atomically: true, encoding: .utf8)
        }
        let expected = try Fixtures.loadHex("usage-openrouter")
        #expect(bytes == expected)
    }

    @Test func cacheKeepsLastGoodOnFailure() {
        var cache = UsageCache()
        let good = cache.recordSuccess(.openRouterFixture)
        #expect(good[3] == 0)
        let failed = cache.recordFailure(capturedAt: Date(timeIntervalSince1970: 1_748_500_000))
        #expect(Array(failed[Protocol.usageHeaderSize...]) == Array(good[Protocol.usageHeaderSize...]))
        #expect((failed[3] & UsageFlags.stale.rawValue) != 0)
        #expect((failed[3] & UsageFlags.bridgeError.rawValue) != 0)
    }

    @Test func unavailableEmptyIsDistinctFromStartupStaleEmpty() {
        let startup = [UInt8](UsageEncoder.staleEmpty())
        let unavailable = [UInt8](UsageEncoder.unavailableEmpty(capturedAt: Date(timeIntervalSince1970: 100)))

        #expect(startup.count == Protocol.usageHeaderSize)
        #expect(unavailable.count == Protocol.usageHeaderSize)
        #expect(startup[2] == 0)
        #expect(unavailable[2] == 0)
        #expect((unavailable[3] & UsageFlags.unavailable.rawValue) != 0)
        #expect(startup[4..<8].allSatisfy { $0 == 0 })
        #expect(unavailable[4] == 100)
    }

    @Test func unknownsBecomeSentinels() {
        let p = NormalizedUsageSpend.Provider(
            kind: .openrouter, status: .ok, currencyCode: "USD",
            todayCost: nil, monthCost: nil, todayTokens: nil, monthTokens: nil,
            todayRequests: nil, monthRequests: nil,
            costHistory: [Double](repeating: 0, count: 30), tokenHistory: [UInt64](repeating: 0, count: 30))
        let bytes = [UInt8](UsageEncoder.encode(.init(capturedAt: Date(timeIntervalSince1970: 0), flags: [], providers: [p])))
        // todayCostMinor..monthRequests (record offsets 12..35) all 0xFF
        #expect(bytes[(12+12)..<(12+36)].allSatisfy { $0 == 0xFF })
    }
}
