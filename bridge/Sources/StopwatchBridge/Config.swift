import Foundation
import Security

public struct Config: Codable, Equatable, Sendable {
    public var codexbarPort: UInt16
    public var serviceUUID: String
    public var logLevel: String
    public var spawnCodexbar: Bool
    public var instanceHash: String
    public var httpBindHost: String
    public var httpPort: UInt16
    public var apiToken: String

    private enum CodingKeys: String, CodingKey {
        case codexbarPort, serviceUUID, logLevel, spawnCodexbar, instanceHash
        case httpBindHost, httpPort, apiToken
    }

    public init(
        codexbarPort: UInt16,
        serviceUUID: String,
        logLevel: String,
        spawnCodexbar: Bool,
        instanceHash: String,
        httpBindHost: String,
        httpPort: UInt16,
        apiToken: String)
    {
        self.codexbarPort = codexbarPort
        self.serviceUUID = serviceUUID
        self.logLevel = logLevel
        self.spawnCodexbar = spawnCodexbar
        self.instanceHash = instanceHash
        self.httpBindHost = httpBindHost
        self.httpPort = httpPort
        self.apiToken = apiToken
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        self.codexbarPort = try c.decode(UInt16.self, forKey: .codexbarPort)
        self.serviceUUID = try c.decode(String.self, forKey: .serviceUUID)
        self.logLevel = try c.decode(String.self, forKey: .logLevel)
        self.spawnCodexbar = try c.decode(Bool.self, forKey: .spawnCodexbar)
        self.instanceHash = try c.decode(String.self, forKey: .instanceHash)
        self.httpBindHost = try c.decodeIfPresent(String.self, forKey: .httpBindHost) ?? "127.0.0.1"
        self.httpPort = try c.decodeIfPresent(UInt16.self, forKey: .httpPort) ?? 8787
        self.apiToken = try c.decodeIfPresent(String.self, forKey: .apiToken) ?? Self.randomHex(byteCount: 32)
    }

    public static let defaultPath: URL = {
        let support = FileManager.default
            .urls(for: .applicationSupportDirectory, in: .userDomainMask)
            .first!
        return support.appendingPathComponent("stopwatch-bridge/config.json")
    }()

    public static func makeDefault() -> Config {
        let port = UInt16.random(in: 49152...65535)
        return .init(
            codexbarPort: port,
            serviceUUID: Protocol.serviceUUID.uuidString,
            logLevel: "info",
            spawnCodexbar: true,
            instanceHash: randomHex(byteCount: 2),
            httpBindHost: "127.0.0.1",
            httpPort: 8787,
            apiToken: randomHex(byteCount: 32)
        )
    }

    public static func load(from url: URL = defaultPath) throws -> Config? {
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }
        let data = try Data(contentsOf: url)
        let cfg = try JSONDecoder().decode(Config.self, from: data)
        if try needsHTTPMigration(data) {
            try save(cfg, to: url)
        }
        return cfg
    }

    public static func save(_ cfg: Config, to url: URL = defaultPath) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(),
                                                withIntermediateDirectories: true)
        let enc = JSONEncoder()
        enc.outputFormatting = [.prettyPrinted, .sortedKeys]
        let prev = umask(0o177)
        defer { umask(prev) }
        try enc.encode(cfg).write(to: url, options: .atomic)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path)
    }

    private static func randomHex(byteCount: Int) -> String {
        var bytes = [UInt8](repeating: 0, count: byteCount)
        let status = SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes)
        if status != errSecSuccess {
            let length = byteCount * 2
            var hex = ""
            while hex.count < length {
                hex += UUID().uuidString.replacingOccurrences(of: "-", with: "")
            }
            return String(hex.prefix(length))
        }
        return bytes.map { String(format: "%02x", $0) }.joined()
    }

    private static func needsHTTPMigration(_ data: Data) throws -> Bool {
        let object = try JSONSerialization.jsonObject(with: data)
        guard let dict = object as? [String: Any] else { return false }
        return dict["httpBindHost"] == nil || dict["httpPort"] == nil || dict["apiToken"] == nil
    }
}
