import Foundation
import Testing

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

    static var fixturesURL: URL { fixturesDir }

    enum FixturesError: Error { case notFound, badHex }
}
