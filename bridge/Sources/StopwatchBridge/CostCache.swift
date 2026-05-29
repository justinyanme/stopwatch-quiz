// bridge/Sources/StopwatchBridge/CostCache.swift
import Foundation

struct CostCache {
    private var lastGood: Data?

    mutating func recordSuccess(_ cost: NormalizedCost) -> Data {
        let snapshot = CostEncoder.encode(cost)
        if !cost.providers.isEmpty && !cost.flags.contains(.bridgeError) {
            lastGood = snapshot
            return snapshot
        }
        return lastGood.map { CostEncoder.markStale($0, capturedAt: cost.capturedAt) } ?? snapshot
    }

    mutating func recordFailure(capturedAt: Date = Date()) -> Data {
        if let lastGood {
            return CostEncoder.markStale(lastGood, capturedAt: capturedAt)
        }
        return CostEncoder.errorEmpty(capturedAt: capturedAt)
    }
}
