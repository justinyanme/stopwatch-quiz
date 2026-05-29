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
        // Decoded fine but empty (e.g. cost tracking off / genuinely no spend): keep the
        // last-known numbers but surface the fresh flags (e.g. costUnavailable), marked
        // stale — this is NOT a bridge error.
        return lastGood.map { CostEncoder.markStale($0, capturedAt: cost.capturedAt, extraFlags: cost.flags) }
            ?? snapshot
    }

    mutating func recordFailure(capturedAt: Date = Date()) -> Data {
        // Fetch threw: keep last-known numbers, flag stale + bridgeError.
        if let lastGood {
            return CostEncoder.markStale(lastGood, capturedAt: capturedAt, extraFlags: .bridgeError)
        }
        return CostEncoder.errorEmpty(capturedAt: capturedAt)
    }
}
