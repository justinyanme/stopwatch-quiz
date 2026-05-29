import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct BalanceEncoderTests {

    @Test func encodesHeaderAndTwoRecords() {
        let data = [UInt8](BalanceEncoder.encode(.balanceFixtureTwo))
        #expect(data.count == 8 + 36 * 2)        // 80
        #expect(data[0] == 1)                     // versionMajor
        #expect(data[1] == 0)                     // versionMinor
        #expect(data[2] == 2)                     // recordCount
        #expect(data[3] == 0)                     // flags
        #expect(le32(data, 4) == 1748455822)      // capturedAt

        let r0 = 8
        #expect(data[r0 + 0] == 1)                // kind = openrouter
        #expect(data[r0 + 1] == 0)                // status = ok
        #expect(data[r0 + 2] == 0)                // recordFlags
        #expect(Array(data[r0+3..<r0+6]) == Array("USD".utf8))
        #expect(data[r0 + 6] == 2)                // decimals
        #expect(le32(data, r0 + 8) == 4210)       // 42.10 → 4210 minor
        #expect(le32(data, r0 + 12) == 790)       // 7.90 → 790
        #expect(le32(data, r0 + 16) == 1748455822)// updatedAt
        #expect(name(data, r0 + 20) == "OpenRouter")

        let r1 = 8 + 36
        #expect(data[r1 + 0] == 2)                // kind = deepseek
        #expect(Array(data[r1+3..<r1+6]) == Array("CNY".utf8))
        #expect(le32(data, r1 + 8) == 31850)      // 318.50 → 31850
        #expect(le32(data, r1 + 12) == 0xFFFF_FFFF) // usage unknown
        #expect(name(data, r1 + 20) == "DeepSeek")
    }

    @Test func unknownUnlimitedAndLowFlag() {
        let p = NormalizedBalance(
            capturedAt: Date(timeIntervalSince1970: 0), flags: [],
            providers: [
                .init(kind: .generic, name: "X", status: .ok, currencyCode: "USD",
                      remaining: nil, updatedAt: nil, isLow: true),
                .init(kind: .generic, name: "Y", status: .ok, currencyCode: "USD",
                      remaining: nil, unlimited: true, updatedAt: nil),
            ])
        let data = [UInt8](BalanceEncoder.encode(p))
        #expect(le32(data, 8 + 8) == 0xFFFF_FFFF)        // record0 remaining unknown
        #expect(data[8 + 2] == 1)                         // record0 low flag
        #expect(le32(data, 8 + 36 + 8) == 0xFFFF_FFFE)   // record1 unlimited
    }

    @Test func clampsToMaxRecords() {
        let many = (0..<20).map { _ in
            NormalizedBalance.Provider(kind: .generic, name: "P", status: .ok,
                                       currencyCode: "USD", remaining: 1, updatedAt: nil)
        }
        let data = BalanceEncoder.encode(.init(capturedAt: Date(), flags: [], providers: many))
        #expect(data[2] == 16)                            // recordCount clamped
        #expect(data.count == 8 + 36 * 16)
    }

    private func le32(_ b: [UInt8], _ o: Int) -> UInt32 {
        UInt32(b[o]) | UInt32(b[o+1]) << 8 | UInt32(b[o+2]) << 16 | UInt32(b[o+3]) << 24
    }
    private func name(_ b: [UInt8], _ o: Int) -> String {
        String(decoding: b[o..<o+16].prefix { $0 != 0 }, as: UTF8.self)
    }
}
