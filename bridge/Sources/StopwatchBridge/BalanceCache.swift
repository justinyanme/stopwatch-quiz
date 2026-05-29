// bridge/Sources/StopwatchBridge/BalanceCache.swift
import Foundation

/// Per-provider last-good cache. A failed provider keeps its previous value
/// (marked stale) so one bad endpoint never blanks the others. Keyed by name
/// (the wire identity shown on the watch).
struct BalanceCache {
    private var lastGood: [String: NormalizedBalance.Provider] = [:]

    mutating func record(_ fresh: NormalizedBalance) -> Data {
        var merged: [NormalizedBalance.Provider] = []
        for p in fresh.providers {
            if p.status == .ok {
                lastGood[p.name] = p
                merged.append(p)
            } else if var prev = lastGood[p.name] {
                prev.status = .stale          // keep last-good numbers, surface staleness
                merged.append(prev)
            } else {
                merged.append(p)              // no history → show the error record as-is
            }
        }
        return BalanceEncoder.encode(NormalizedBalance(capturedAt: fresh.capturedAt,
                                                       flags: fresh.flags, providers: merged))
    }

    mutating func recordFailure(capturedAt: Date = Date()) -> Data {
        // Whole-cycle failure: re-emit last-good (all stale) + bridgeError, or an empty error frame.
        guard !lastGood.isEmpty else { return BalanceEncoder.errorEmpty(capturedAt: capturedAt) }
        let stale = lastGood.values.map { p -> NormalizedBalance.Provider in
            var q = p; q.status = .stale; return q
        }
        return BalanceEncoder.encode(NormalizedBalance(capturedAt: capturedAt,
                                                       flags: [.stale, .bridgeError], providers: stale))
    }
}
