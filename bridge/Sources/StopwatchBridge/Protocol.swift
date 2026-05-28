@preconcurrency import CoreBluetooth
import Foundation

/// Wire-protocol constants. Mirrors `shared/PROTOCOL.md`. Any change here MUST
/// land alongside the matching change in `firmware/src/Protocol.h`.
public enum Protocol {
    public static let serviceUUID   = CBUUID(string: "91412041-D927-4633-A0ED-B066DF91EE55")
    public static let snapshotUUID  = CBUUID(string: "621645B4-14D2-4E58-975B-73B81D43916D")
    public static let triggerUUID   = CBUUID(string: "6817329E-A603-4A34-BB4D-04215218304C")

    public static let localName     = "Stopwatch Bridge"
    public static let versionMajor: UInt8 = 1
    public static let versionMinor: UInt8 = 0

    public static let headerSize       = 8
    public static let perProviderSize  = 16
    public static let providerCount    = 3
    public static let snapshotSize     = headerSize + perProviderSize * providerCount  // 56
}

public enum SnapshotFlag: UInt8, Sendable {
    case stale            = 0b0000_0001
    case bridgeError      = 0b0000_0010
    case providerMissing  = 0b0000_0100
}

public enum ProviderID: UInt8, Sendable {
    case codex  = 1
    case claude = 2
    case gemini = 3
}

public enum ProviderStatus: UInt8, Sendable {
    case ok = 0, warn = 1, critical = 2, error = 3, disabled = 4
}

public enum ProviderPlan: UInt8, Sendable {
    case unknown = 0, free = 1, plus = 2, pro = 3, team = 4, enterprise = 5

    public init(fromString s: String?) {
        guard let s else { self = .unknown; return }
        switch s.lowercased() {
        case "free":       self = .free
        case "plus":       self = .plus
        case "pro":        self = .pro
        case "team":       self = .team
        case "enterprise": self = .enterprise
        default:           self = .unknown
        }
    }
}

/// Normalized input that `SnapshotEncoder` consumes. `CodexbarClient` produces
/// this by transforming the live `codexbar serve` response.
public struct NormalizedUsage: Equatable, Sendable {
    public struct Provider: Equatable, Sendable {
        public var providerID: ProviderID
        public var status: ProviderStatus
        public var sessionPct: UInt8?       // nil → 0xFF on the wire
        public var weekPct: UInt8?
        public var sessionResetAt: Date?    // nil → 0 on the wire
        public var weekResetAt: Date?
        public var credits: Double?         // nil → 0xFFFF on the wire
        public var plan: ProviderPlan
    }

    public var capturedAt: Date
    public var flags: Set<SnapshotFlag>
    public var providers: [Provider]
}
