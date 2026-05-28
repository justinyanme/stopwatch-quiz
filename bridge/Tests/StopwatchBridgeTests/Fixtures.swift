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
