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

    public static let costSnapshotUUID = CBUUID(string: "33FAAC2D-3935-467F-A0A0-899CE2306366")
    public static let costVersionMajor: UInt8 = 1
    public static let costVersionMinor: UInt8 = 0
    public static let costHeaderSize    = 12
    public static let costRecordSize    = 60
    public static let costHistoryDays   = 30
    public static let triggerScopeCost: UInt8 = 0x04

    public static let balanceSnapshotUUID = CBUUID(string: "4D9E8F21-7C3A-4B6D-8E15-9A2F6C3B0D74")
    public static let balanceVersionMajor: UInt8 = 1
    public static let balanceVersionMinor: UInt8 = 0
    public static let balanceHeaderSize  = 8
    public static let balanceRecordSize  = 36
    public static let balanceMaxRecords  = 16
    public static let triggerScopeBalances: UInt8 = 0x05
    public static let balanceRecordFlagLow: UInt8 = 0b0000_0001

    public static let usageSnapshotUUID = CBUUID(string: "7E2C5A19-4B8F-4D3A-9E61-2F7A8C0B5D34")
    public static let usageVersionMajor: UInt8 = 1
    public static let usageVersionMinor: UInt8 = 0
    public static let usageHeaderSize   = 12
    public static let usageRecordSize   = 96
    public static let usageHistoryDays  = 30
    public static let usageMaxRecords   = 4
    public static let triggerScopeUsage: UInt8 = 0x06
}

public struct SnapshotFlags: OptionSet, Sendable {
    public let rawValue: UInt8
    public init(rawValue: UInt8) { self.rawValue = rawValue }

    public static let stale           = SnapshotFlags(rawValue: 0b0000_0001)
    public static let bridgeError     = SnapshotFlags(rawValue: 0b0000_0010)
    public static let providerMissing = SnapshotFlags(rawValue: 0b0000_0100)
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

public struct BalanceFlags: OptionSet, Sendable {
    public let rawValue: UInt8
    public init(rawValue: UInt8) { self.rawValue = rawValue }

    public static let stale       = BalanceFlags(rawValue: 0b0000_0001)
    public static let bridgeError = BalanceFlags(rawValue: 0b0000_0010)
}

public enum BalanceKind: UInt8, Sendable {
    case generic = 0, openrouter = 1, deepseek = 2, groq = 3, together = 4
    case fireworks = 5, siliconflow = 6, moonshot = 7, zhipu = 8

    public init(fromString s: String?) {
        switch (s ?? "").lowercased() {
        case "openrouter":  self = .openrouter
        case "deepseek":    self = .deepseek
        case "groq":        self = .groq
        case "together":    self = .together
        case "fireworks":   self = .fireworks
        case "siliconflow": self = .siliconflow
        case "moonshot":    self = .moonshot
        case "zhipu":       self = .zhipu
        default:            self = .generic
        }
    }
}

/// Per-record status. `stale` here is record-level (this provider's data is from
/// a prior poll cycle) — distinct from `BalanceFlags.stale`, which marks the
/// whole snapshot stale.
public enum BalanceStatus: UInt8, Sendable {
    case ok = 0, stale = 1, authError = 2, unreachable = 3, depleted = 4
}

/// Normalized balance input that `BalanceEncoder` consumes.
public struct NormalizedBalance: Equatable, Sendable {
    public struct Provider: Equatable, Sendable {
        public var kind: BalanceKind
        public var name: String
        public var status: BalanceStatus
        public var currencyCode: String      // 1–3 ASCII chars; "" = unknown
        public var currencyDecimals: Int     // default 2
        public var remaining: Double?        // nil → 0xFFFFFFFF (unknown)
        public var unlimited: Bool           // true → 0xFFFFFFFE
        public var usage: Double?            // nil → 0xFFFFFFFF
        public var updatedAt: Date?          // nil → 0
        public var isLow: Bool               // → recordFlags bit0

        public init(kind: BalanceKind, name: String, status: BalanceStatus,
                    currencyCode: String, currencyDecimals: Int = 2,
                    remaining: Double?, unlimited: Bool = false, usage: Double? = nil,
                    updatedAt: Date?, isLow: Bool = false) {
            self.kind = kind; self.name = name; self.status = status
            self.currencyCode = currencyCode; self.currencyDecimals = currencyDecimals
            self.remaining = remaining; self.unlimited = unlimited; self.usage = usage
            self.updatedAt = updatedAt; self.isLow = isLow
        }
    }
    public var capturedAt: Date
    public var flags: BalanceFlags
    public var providers: [Provider]

    public init(capturedAt: Date, flags: BalanceFlags, providers: [Provider]) {
        self.capturedAt = capturedAt; self.flags = flags; self.providers = providers
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
        /// Credits remaining. `nil` → `0xFFFF` (unknown) on the wire.
        /// Precision: one decimal place (×10 encoding). Max representable: 6553.4 (0xFFFE).
        /// Values above 6553.4 are clamped at encode time.
        public var credits: Double?
        public var plan: ProviderPlan
    }

    public var capturedAt: Date
    public var flags: SnapshotFlags
    public var providers: [Provider]
}
