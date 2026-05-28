// bridge/Tests/StopwatchBridgeTests/SnapshotEncoderTests.swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct SnapshotEncoderTests {

    @Test func headerSizeAndTotalSizeMatchProtocol() throws {
        let input = NormalizedUsage(
            capturedAt: Date(timeIntervalSince1970: 1748455822),
            flags: [],
            providers: [
                .init(providerID: .codex,  status: .ok, sessionPct: 72, weekPct: 41,
                      sessionResetAt: Date(timeIntervalSince1970: 1748467200),
                      weekResetAt:    Date(timeIntervalSince1970: 1748538000),
                      credits: 112.4, plan: .plus),
                .init(providerID: .claude, status: .ok, sessionPct: 12, weekPct: 37,
                      sessionResetAt: Date(timeIntervalSince1970: 1748502000),
                      weekResetAt:    Date(timeIntervalSince1970: 1748696400),
                      credits: nil, plan: .pro),
                .init(providerID: .gemini, status: .ok, sessionPct: 8, weekPct: nil,
                      sessionResetAt: Date(timeIntervalSince1970: 1748476800),
                      weekResetAt: nil,
                      credits: nil, plan: .free),
            ]
        )

        let bytes = SnapshotEncoder.encode(input)
        #expect(bytes.count == Protocol.snapshotSize)
        #expect(bytes[0] == Protocol.versionMajor)
        #expect(bytes[1] == Protocol.versionMinor)
        #expect(bytes[2] == 3)
        #expect(bytes[3] == 0)  // flags
    }

    @Test func unknownNumericsEncodeAsSentinels() {
        let input = NormalizedUsage(
            capturedAt: Date(timeIntervalSince1970: 0),
            flags: [.stale, .bridgeError],
            providers: [
                .init(providerID: .codex, status: .error,
                      sessionPct: nil, weekPct: nil,
                      sessionResetAt: nil, weekResetAt: nil,
                      credits: nil, plan: .unknown),
                .init(providerID: .claude, status: .disabled,
                      sessionPct: nil, weekPct: nil,
                      sessionResetAt: nil, weekResetAt: nil,
                      credits: nil, plan: .unknown),
                .init(providerID: .gemini, status: .disabled,
                      sessionPct: nil, weekPct: nil,
                      sessionResetAt: nil, weekResetAt: nil,
                      credits: nil, plan: .unknown),
            ]
        )

        let bytes = SnapshotEncoder.encode(input)
        // flags byte: bit0 (stale) | bit1 (bridgeError) = 0b011 = 0x03
        #expect(bytes[3] == 0x03)
        // First provider record starts at offset 8.
        #expect(bytes[8] == ProviderID.codex.rawValue)
        #expect(bytes[9] == ProviderStatus.error.rawValue)
        #expect(bytes[10] == 0xFF)
        #expect(bytes[11] == 0xFF)
        // sessionResetAt + weekResetAt: 8 bytes of 0
        #expect(bytes[12..<20].allSatisfy { $0 == 0 })
        // credits sentinel: 0xFFFF (LE)
        #expect(bytes[20] == 0xFF && bytes[21] == 0xFF)
        #expect(bytes[22] == ProviderPlan.unknown.rawValue)
        #expect(bytes[23] == 0)  // reserved
    }

    @Test func creditsAreScaledByTen() {
        let input = NormalizedUsage(
            capturedAt: Date(timeIntervalSince1970: 0),
            flags: [],
            providers: [
                .init(providerID: .codex, status: .ok, sessionPct: 0, weekPct: 0,
                      sessionResetAt: nil, weekResetAt: nil,
                      credits: 112.4, plan: .plus),
                .init(providerID: .claude, status: .ok, sessionPct: 0, weekPct: 0,
                      sessionResetAt: nil, weekResetAt: nil,
                      credits: nil, plan: .pro),
                .init(providerID: .gemini, status: .ok, sessionPct: 0, weekPct: 0,
                      sessionResetAt: nil, weekResetAt: nil,
                      credits: nil, plan: .free),
            ]
        )
        let bytes = SnapshotEncoder.encode(input)
        // 1124 LE = 0x64 0x04
        #expect(bytes[20] == 0x64)
        #expect(bytes[21] == 0x04)
    }

    @Test func threeProvidersFixtureMatchesSavedHex() throws {
        let expected = try Fixtures.loadHex("codexbar-three-providers")
        let actual = SnapshotEncoder.encode(.threeProvidersFixture)
        #expect(actual == expected)
    }

    @Test func codexOnlyFixtureMatchesSavedHex() throws {
        let expected = try Fixtures.loadHex("codexbar-codex-only")
        let actual = SnapshotEncoder.encode(.codexOnlyFixture)
        #expect(actual == expected)
    }

    @Test func errorFixtureMatchesSavedHex() throws {
        let expected = try Fixtures.loadHex("codexbar-error")
        let actual = SnapshotEncoder.encode(.errorFixture)
        #expect(actual == expected)
    }

    @Test func gattSnapshotPadsScopedFetchToFixedPayload() {
        let bytes = SnapshotEncoder.encodeGATTSnapshot(.codexOnlyFixture)

        #expect(bytes.count == Protocol.snapshotSize)
        #expect(bytes[2] == UInt8(Protocol.providerCount))
        #expect((bytes[3] & SnapshotFlags.providerMissing.rawValue) != 0)

        let claudeOffset = Protocol.headerSize + Protocol.perProviderSize
        let geminiOffset = Protocol.headerSize + Protocol.perProviderSize * 2
        #expect(bytes[claudeOffset] == ProviderID.claude.rawValue)
        #expect(bytes[claudeOffset + 1] == ProviderStatus.disabled.rawValue)
        #expect(bytes[geminiOffset] == ProviderID.gemini.rawValue)
        #expect(bytes[geminiOffset + 1] == ProviderStatus.disabled.rawValue)
    }

    @Test func snapshotCacheKeepsLastGoodProviderBytesOnFailure() {
        var cache = SnapshotCache()
        let good = cache.recordSuccess(.threeProvidersFixture)

        let failed = cache.recordFailure()

        #expect(failed.count == Protocol.snapshotSize)
        #expect((failed[3] & SnapshotFlags.stale.rawValue) != 0)
        #expect((failed[3] & SnapshotFlags.bridgeError.rawValue) != 0)
        #expect(Array(failed[Protocol.headerSize...]) == Array(good[Protocol.headerSize...]))
    }

    @Test func snapshotCacheDoesNotReplaceLastGoodDataWithBridgeErrorPayload() {
        var cache = SnapshotCache()
        let good = cache.recordSuccess(.threeProvidersFixture)

        _ = cache.recordSuccess(.errorFixture)
        let failed = cache.recordFailure()

        #expect(Array(failed[Protocol.headerSize...]) == Array(good[Protocol.headerSize...]))
    }
}
