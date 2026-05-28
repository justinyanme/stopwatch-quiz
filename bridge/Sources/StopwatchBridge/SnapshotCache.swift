// bridge/Sources/StopwatchBridge/SnapshotCache.swift
import Foundation

struct SnapshotCache {
    private var lastGoodSnapshot: Data?

    mutating func recordSuccess(_ usage: NormalizedUsage) -> Data {
        let snapshot = SnapshotEncoder.encodeGATTSnapshot(usage)
        if isValidUsagePayload(usage) {
            lastGoodSnapshot = snapshot
        }
        return snapshot
    }

    mutating func recordFailure(capturedAt: Date = Date()) -> Data {
        if let lastGoodSnapshot {
            return SnapshotEncoder.markStaleBridgeError(lastGoodSnapshot)
        }
        return SnapshotEncoder.errorEmpty(capturedAt: capturedAt)
    }

    private func isValidUsagePayload(_ usage: NormalizedUsage) -> Bool {
        !usage.providers.isEmpty && !usage.flags.contains(.bridgeError)
    }
}
