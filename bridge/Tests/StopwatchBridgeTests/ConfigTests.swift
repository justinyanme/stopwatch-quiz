import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct ConfigTests {

    @Test func roundTripsDefaults() throws {
        let tmp = FileManager.default.temporaryDirectory.appendingPathComponent("cfg-\(UUID()).json")
        defer { try? FileManager.default.removeItem(at: tmp) }

        let original = Config.makeDefault()
        try Config.save(original, to: tmp)

        let loaded = try Config.load(from: tmp)
        #expect(loaded == original)
        #expect(loaded!.codexbarPort >= 49152 && loaded!.codexbarPort <= 65535)
        #expect(loaded!.spawnCodexbar == true)
        #expect(loaded!.instanceHash.count == 4)  // 2 bytes → 4 hex chars
    }

    @Test func loadingMissingFileReturnsNil() throws {
        let tmp = FileManager.default.temporaryDirectory.appendingPathComponent("nope-\(UUID()).json")
        let loaded = try Config.load(from: tmp)
        #expect(loaded == nil)
    }

    @Test func defaultsIncludeHTTPServerSettings() throws {
        let cfg = Config.makeDefault()
        #expect(cfg.httpBindHost == "127.0.0.1")
        #expect(cfg.httpPort == 8787)
        #expect(cfg.apiToken.count == 64)
        #expect(cfg.apiToken.allSatisfy { $0.isHexDigit })
    }

    @Test func decodesLegacyConfigWithHTTPDefaults() throws {
        let legacy = Data("""
        {
          "codexbarPort": 54321,
          "serviceUUID": "\(Protocol.serviceUUID.uuidString)",
          "logLevel": "info",
          "spawnCodexbar": true,
          "instanceHash": "abcd"
        }
        """.utf8)

        let decoded = try JSONDecoder().decode(Config.self, from: legacy)
        #expect(decoded.codexbarPort == 54321)
        #expect(decoded.httpBindHost == "127.0.0.1")
        #expect(decoded.httpPort == 8787)
        #expect(decoded.apiToken.count == 64)
    }

    @Test func loadingLegacyConfigPersistsMigratedHTTPSettings() throws {
        let tmp = FileManager.default.temporaryDirectory.appendingPathComponent("legacy-cfg-\(UUID()).json")
        defer { try? FileManager.default.removeItem(at: tmp) }
        try Data("""
        {
          "codexbarPort": 54321,
          "serviceUUID": "\(Protocol.serviceUUID.uuidString)",
          "logLevel": "info",
          "spawnCodexbar": true,
          "instanceHash": "abcd"
        }
        """.utf8).write(to: tmp)

        let first = try Config.load(from: tmp)
        let second = try Config.load(from: tmp)
        #expect(first != nil)
        #expect(second != nil)
        let firstToken = first?.apiToken ?? ""
        #expect(firstToken == second?.apiToken)
        #expect(firstToken.count == 64)
        #expect(firstToken.allSatisfy { $0.isHexDigit })

        let persistedObject = try JSONSerialization.jsonObject(with: Data(contentsOf: tmp))
        let persisted = try #require(persistedObject as? [String: Any])
        #expect(persisted["apiToken"] as? String == firstToken)
        #expect(persisted["httpBindHost"] as? String == "127.0.0.1")
        #expect(persisted["httpPort"] as? Int == 8787)
    }

    @Test func loadingConfigWithNullHTTPTokenPersistsMigratedToken() throws {
        let tmp = FileManager.default.temporaryDirectory.appendingPathComponent("null-http-cfg-\(UUID()).json")
        defer { try? FileManager.default.removeItem(at: tmp) }
        try Data("""
        {
          "apiToken": null,
          "codexbarPort": 54321,
          "httpBindHost": "127.0.0.1",
          "httpPort": 8787,
          "serviceUUID": "\(Protocol.serviceUUID.uuidString)",
          "logLevel": "info",
          "spawnCodexbar": true,
          "instanceHash": "abcd"
        }
        """.utf8).write(to: tmp)

        let first = try Config.load(from: tmp)
        let second = try Config.load(from: tmp)
        #expect(first != nil)
        #expect(second != nil)
        let firstToken = first?.apiToken ?? ""
        #expect(firstToken == second?.apiToken)
        #expect(firstToken.count == 64)
        #expect(firstToken.allSatisfy { $0.isHexDigit })

        let persistedObject = try JSONSerialization.jsonObject(with: Data(contentsOf: tmp))
        let persisted = try #require(persistedObject as? [String: Any])
        #expect(persisted["apiToken"] as? String == firstToken)
    }
}
