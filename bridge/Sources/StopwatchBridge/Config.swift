import Foundation
import Security

public struct Config: Codable, Equatable, Sendable {
    public var codexbarPort: UInt16
    public var serviceUUID: String
    public var logLevel: String
    public var spawnCodexbar: Bool
    public var instanceHash: String  // 2-byte hex, e.g. "ab12"

    public static let defaultPath: URL = {
        let support = FileManager.default
            .urls(for: .applicationSupportDirectory, in: .userDomainMask)
            .first!
        return support.appendingPathComponent("stopwatch-bridge/config.json")
    }()

    public static func makeDefault() -> Config {
        // Random ephemeral port via the same range as `jot -r 1 49152 65535`.
        let port = UInt16.random(in: 49152...65535)
        var hashBytes = [UInt8](repeating: 0, count: 2)
        let status = SecRandomCopyBytes(kSecRandomDefault, hashBytes.count, &hashBytes)
        if status != errSecSuccess {
            // Extremely rare; fall back to first 2 bytes of a UUID so the hash is still unique.
            let uuid = UUID().uuid
            hashBytes = [uuid.0, uuid.1]
        }
        let hash = hashBytes.map { String(format: "%02x", $0) }.joined()
        return .init(
            codexbarPort: port,
            serviceUUID: Protocol.serviceUUID.uuidString,
            logLevel: "info",
            spawnCodexbar: true,
            instanceHash: hash
        )
    }

    public static func load(from url: URL = defaultPath) throws -> Config? {
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }
        let data = try Data(contentsOf: url)
        return try JSONDecoder().decode(Config.self, from: data)
    }

    public static func save(_ cfg: Config, to url: URL = defaultPath) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(),
                                                withIntermediateDirectories: true)
        let enc = JSONEncoder()
        enc.outputFormatting = [.prettyPrinted, .sortedKeys]
        // Set umask so the atomic write creates the file with 0o600 directly,
        // closing the TOCTOU window before the explicit chmod below.
        let prev = umask(0o177)
        defer { umask(prev) }
        try enc.encode(cfg).write(to: url, options: .atomic)
        // Belt-and-suspenders chmod in case rename swap preserved old perms.
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path)
    }
}
