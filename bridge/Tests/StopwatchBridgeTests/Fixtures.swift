import Foundation
import Testing
@testable import StopwatchBridge

enum Fixtures {

    /// Repo-root-relative path to shared/fixtures, derived from this file's location.
    /// This file lives at: <repo>/bridge/Tests/StopwatchBridgeTests/Fixtures.swift
    private static let fixturesDir: URL = {
        URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()  // Tests/StopwatchBridgeTests
            .deletingLastPathComponent()  // Tests
            .deletingLastPathComponent()  // bridge
            .deletingLastPathComponent()  // <repo>
            .appendingPathComponent("shared/fixtures")
    }()

    static func load(_ name: String, ext: String) throws -> Data {
        let url = fixturesDir.appendingPathComponent("\(name).\(ext)")
        guard FileManager.default.fileExists(atPath: url.path) else {
            Issue.record("missing fixture at \(url.path)")
            throw FixturesError.notFound
        }
        return try Data(contentsOf: url)
    }

    static func loadJSON(_ name: String) throws -> Data {
        try load(name, ext: "json")
    }

    static func loadHex(_ name: String) throws -> Data {
        let raw = try load(name, ext: "hex")
        let cleaned = String(decoding: raw, as: UTF8.self)
            .filter { !$0.isWhitespace }
        guard cleaned.count % 2 == 0 else {
            Issue.record("hex string has odd character count (\(cleaned.count))")
            throw FixturesError.badHex
        }
        var bytes = Data()
        var i = cleaned.startIndex
        while i < cleaned.endIndex {
            let next = cleaned.index(i, offsetBy: 2, limitedBy: cleaned.endIndex) ?? cleaned.endIndex
            guard let byte = UInt8(cleaned[i..<next], radix: 16) else {
                Issue.record("bad hex byte at offset \(cleaned.distance(from: cleaned.startIndex, to: i))")
                throw FixturesError.badHex
            }
            bytes.append(byte)
            i = next
        }
        return bytes
    }

    enum FixturesError: Error { case notFound, badHex }
}

extension NormalizedUsage {
    static var threeProvidersFixture: NormalizedUsage {
        .init(
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
    }

    static var codexOnlyFixture: NormalizedUsage {
        .init(
            capturedAt: Date(timeIntervalSince1970: 1748455822),
            flags: [],
            providers: [
                .init(providerID: .codex, status: .ok, sessionPct: 72, weekPct: 41,
                      sessionResetAt: Date(timeIntervalSince1970: 1748467200),
                      weekResetAt:    Date(timeIntervalSince1970: 1748538000),
                      credits: 112.4, plan: .plus),
            ]
        )
    }

    static var errorFixture: NormalizedUsage {
        .init(
            capturedAt: Date(timeIntervalSince1970: 1748455822),
            flags: [.stale, .bridgeError],
            providers: []
        )
    }
}

extension NormalizedCost {
    /// Controlled round-number fixture for byte-exact encoder assertions.
    /// max day = $120 (codex) ⇒ historyUnitCents = ceil(12000/255) = 48.
    static var costFixtureTwo: NormalizedCost {
        var codexHist = [Double](repeating: 0, count: 30); codexHist[29] = 120.0
        var claudeHist = [Double](repeating: 0, count: 30); claudeHist[29] = 60.0
        return .init(
            capturedAt: Date(timeIntervalSince1970: 1748455822),
            flags: [],
            providers: [
                .init(providerID: .codex,  todayCostUSD: 12.0, monthCostUSD: 300.0,
                      todayTokens: 1_000_000, monthTokens: 100_000_000,
                      topModel: "gpt-5.5", history: codexHist),
                .init(providerID: .claude, todayCostUSD: 8.0,  monthCostUSD: 200.0,
                      todayTokens: 2_000_000, monthTokens: 50_000_000,
                      topModel: "claude-opus-4-7", history: claudeHist),
            ]
        )
    }
}

extension NormalizedUsageSpend {
    /// Round-number fixture for byte-exact cross-side assertions.
    static var openRouterFixture: NormalizedUsageSpend {
        var cost = [Double](repeating: 0, count: 30); cost[28] = 50.0; cost[29] = 100.0
        var tok  = [UInt64](repeating: 0, count: 30);  tok[28] = 500_000; tok[29] = 1_000_000
        return .init(capturedAt: Date(timeIntervalSince1970: 1_748_455_822), flags: [], providers: [
            .init(kind: .openrouter, status: .ok, currencyCode: "USD", currencyDecimals: 2,
                  todayCost: 100.0, monthCost: 150.0, todayTokens: 1_000_000, monthTokens: 1_500_000,
                  todayRequests: 1240, monthRequests: 9000, costHistory: cost, tokenHistory: tok)
        ])
    }
}

extension NormalizedBalance {
    /// Round-number fixture for byte-exact encoder assertions.
    static var balanceFixtureTwo: NormalizedBalance {
        .init(
            capturedAt: Date(timeIntervalSince1970: 1748455822),
            flags: [],
            providers: [
                .init(kind: .openrouter, name: "OpenRouter", status: .ok,
                      currencyCode: "USD", currencyDecimals: 2,
                      remaining: 42.10, usage: 7.90,
                      updatedAt: Date(timeIntervalSince1970: 1748455822), isLow: false),
                .init(kind: .deepseek, name: "DeepSeek", status: .ok,
                      currencyCode: "CNY", currencyDecimals: 2,
                      remaining: 318.50, usage: nil,
                      updatedAt: Date(timeIntervalSince1970: 1748455822), isLow: false),
            ]
        )
    }
}
