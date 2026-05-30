// bridge/Sources/StopwatchBridge/UsageCache.swift
import Foundation

struct UsageCache {
    private var lastGood: Data?

    mutating func recordSuccess(_ usage: NormalizedUsageSpend) -> Data {
        let snapshot = UsageEncoder.encode(usage)
        let anyOk = usage.providers.contains { $0.status == .ok }
        if anyOk && !usage.flags.contains(.bridgeError) {
            lastGood = snapshot
            return snapshot
        }
        return lastGood.map { UsageEncoder.markStale($0, capturedAt: usage.capturedAt, extraFlags: usage.flags) }
            ?? snapshot
    }

    mutating func recordFailure(capturedAt: Date = Date()) -> Data {
        if let lastGood {
            return UsageEncoder.markStale(lastGood, capturedAt: capturedAt, extraFlags: .bridgeError)
        }
        return UsageEncoder.errorEmpty(capturedAt: capturedAt)
    }
}
