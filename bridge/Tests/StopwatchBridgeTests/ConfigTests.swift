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
}
