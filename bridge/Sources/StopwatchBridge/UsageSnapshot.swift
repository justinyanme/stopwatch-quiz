// bridge/Sources/StopwatchBridge/UsageSnapshot.swift
import Foundation

public struct UsageFlags: OptionSet, Sendable {
    public let rawValue: UInt8
    public init(rawValue: UInt8) { self.rawValue = rawValue }
    public static let stale       = UsageFlags(rawValue: 0b0000_0001)
    public static let bridgeError = UsageFlags(rawValue: 0b0000_0010)
    public static let unavailable = UsageFlags(rawValue: 0b0000_0100)
}

/// Normalized per-provider usage/spend that `UsageEncoder` consumes.
public struct NormalizedUsageSpend: Equatable, Sendable {
    public struct Provider: Equatable, Sendable {
        public var kind: BalanceKind
        public var status: BalanceStatus
        public var currencyCode: String        // 1–3 ASCII chars
        public var currencyDecimals: Int
        public var todayCost: Double?          // currency units; nil → unknown
        public var monthCost: Double?
        public var todayTokens: UInt64?
        public var monthTokens: UInt64?
        public var todayRequests: UInt64?
        public var monthRequests: UInt64?
        public var costHistory: [Double]       // length 30, currency/day, index 29 = capturedAt day
        public var tokenHistory: [UInt64]      // length 30, tokens/day

        public init(kind: BalanceKind, status: BalanceStatus, currencyCode: String,
                    currencyDecimals: Int = 2, todayCost: Double?, monthCost: Double?,
                    todayTokens: UInt64?, monthTokens: UInt64?, todayRequests: UInt64?,
                    monthRequests: UInt64?, costHistory: [Double], tokenHistory: [UInt64]) {
            self.kind = kind; self.status = status
            self.currencyCode = currencyCode; self.currencyDecimals = currencyDecimals
            self.todayCost = todayCost; self.monthCost = monthCost
            self.todayTokens = todayTokens; self.monthTokens = monthTokens
            self.todayRequests = todayRequests; self.monthRequests = monthRequests
            self.costHistory = costHistory; self.tokenHistory = tokenHistory
        }
    }
    public var capturedAt: Date
    public var flags: UsageFlags
    public var providers: [Provider]
    public init(capturedAt: Date, flags: UsageFlags, providers: [Provider]) {
        self.capturedAt = capturedAt; self.flags = flags; self.providers = providers
    }
}
