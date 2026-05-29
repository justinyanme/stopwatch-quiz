// bridge/Sources/StopwatchBridge/CostSnapshot.swift
import Foundation

public struct CostFlags: OptionSet, Sendable {
    public let rawValue: UInt8
    public init(rawValue: UInt8) { self.rawValue = rawValue }

    public static let stale           = CostFlags(rawValue: 0b0000_0001)
    public static let bridgeError     = CostFlags(rawValue: 0b0000_0010)
    public static let costUnavailable = CostFlags(rawValue: 0b0000_0100)
}

/// Normalized `/cost` input that `CostEncoder` consumes. `CodexbarClient.fetchCost`
/// produces this from the live `codexbar serve` `/cost` response.
public struct NormalizedCost: Equatable, Sendable {
    public struct Provider: Equatable, Sendable {
        public var providerID: ProviderID
        public var todayCostUSD: Double?      // nil → 0xFFFFFFFF on the wire
        public var monthCostUSD: Double?
        public var todayTokens: UInt64?
        public var monthTokens: UInt64?
        public var topModel: String?          // full name; encoder shortens to 12 chars
        public var history: [Double]          // dense, length 30, USD/day, index 29 = capturedAt day

        public init(providerID: ProviderID, todayCostUSD: Double?, monthCostUSD: Double?,
                    todayTokens: UInt64?, monthTokens: UInt64?, topModel: String?, history: [Double]) {
            self.providerID = providerID
            self.todayCostUSD = todayCostUSD
            self.monthCostUSD = monthCostUSD
            self.todayTokens = todayTokens
            self.monthTokens = monthTokens
            self.topModel = topModel
            self.history = history
        }
    }

    public var capturedAt: Date
    public var flags: CostFlags
    public var providers: [Provider]

    public init(capturedAt: Date, flags: CostFlags, providers: [Provider]) {
        self.capturedAt = capturedAt
        self.flags = flags
        self.providers = providers
    }
}
