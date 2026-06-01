// bridge/Sources/StopwatchBridge/CostEncoder.swift
import Foundation

public enum CostEncoder {

    /// Encodes normalized cost into the `12 + 85*N` byte CostSnapshot wire format.
    /// See `shared/PROTOCOL.md §3A`.
    public static func encode(_ cost: NormalizedCost) -> Data {
        let unitCents = sharedUnitCents(cost.providers)

        var out = Data(capacity: Protocol.costHeaderSize + Protocol.costRecordSize * cost.providers.count)
        out.append(Protocol.costVersionMajor)
        out.append(Protocol.costVersionMinor)
        precondition(cost.providers.count <= 255, "too many cost records")
        out.append(UInt8(cost.providers.count))
        out.append(cost.flags.rawValue)
        appendU32(&out, UInt32(max(0, cost.capturedAt.timeIntervalSince1970)))
        out.append(UInt8(Protocol.costHistoryDays))
        out.append(0)  // reserved
        appendU16(&out, unitCents)

        for p in cost.providers {
            out.append(p.providerID.rawValue)
            out.append(0)  // reserved
            appendU32(&out, centsOrUnknown(p.todayCostUSD))
            appendU32(&out, centsOrUnknown(p.monthCostUSD))
            appendU32(&out, tokensOrUnknown(p.todayTokens))
            appendU32(&out, tokensOrUnknown(p.monthTokens))
            appendModels(&out, p.models)
            appendHistory(&out, p.history, unitCents: unitCents)
        }
        return out
    }

    public static func staleEmpty() -> Data {
        encode(NormalizedCost(capturedAt: Date(timeIntervalSince1970: 0),
                              flags: [.stale, .costUnavailable], providers: []))
    }

    public static func errorEmpty(capturedAt: Date = Date()) -> Data {
        encode(NormalizedCost(capturedAt: capturedAt,
                              flags: [.stale, .bridgeError], providers: []))
    }

    /// Sets the `stale` flag (plus any `extraFlags`) and refreshes capturedAt on an
    /// already-encoded snapshot, preserving the existing record bytes.
    public static func markStale(_ snapshot: Data, capturedAt: Date, extraFlags: CostFlags = []) -> Data {
        guard snapshot.count >= Protocol.costHeaderSize else { return snapshot }
        var out = snapshot
        out[3] |= CostFlags.stale.rawValue | extraFlags.rawValue
        writeU32(&out, UInt32(max(0, capturedAt.timeIntervalSince1970)), at: 4)
        return out
    }

    /// Strips a known vendor prefix, then truncates to 11 chars (+ null = 12).
    static func shortenModel(_ name: String) -> String {
        var s = name
        for prefix in ["claude-", "openai-", "google-", "gemini-"] {
            if s.hasPrefix(prefix) { s.removeFirst(prefix.count); break }
        }
        return String(s.prefix(11))
    }

    // MARK: - helpers

    private static func sharedUnitCents(_ providers: [NormalizedCost.Provider]) -> UInt16 {
        var maxCents = 0
        for p in providers {
            for usd in p.history {
                maxCents = max(maxCents, Int((usd * 100).rounded()))
            }
        }
        if maxCents <= 0 { return 1 }
        let unit = (maxCents + 254) / 255   // ceil(maxCents / 255)
        return UInt16(min(max(unit, 1), 65535))
    }

    private static func centsOrUnknown(_ usd: Double?) -> UInt32 {
        guard let usd else { return 0xFFFF_FFFF }
        let cents = (usd * 100).rounded()
        if cents < 0 { return 0 }
        if cents >= Double(UInt32.max) { return 0xFFFF_FFFE }
        return UInt32(cents)
    }

    private static func tokensOrUnknown(_ tokens: UInt64?) -> UInt32 {
        guard let tokens else { return 0xFFFF_FFFF }
        return tokens >= UInt64(UInt32.max) ? 0xFFFF_FFFE : UInt32(tokens)
    }

    private static func appendModels(_ out: inout Data, _ models: [String]) {
        out.append(UInt8(min(models.count, 255)))   // total count for +N overflow
        for i in 0..<Protocol.costMaxModelSlots {
            var field = [UInt8](repeating: 0, count: 12)
            if i < models.count {
                let bytes = Array(shortenModel(models[i]).utf8.prefix(11))
                for (j, b) in bytes.enumerated() { field[j] = b }
            }
            out.append(contentsOf: field)
        }
    }

    private static func appendHistory(_ out: inout Data, _ history: [Double], unitCents: UInt16) {
        let unit = Double(unitCents)
        for i in 0..<Protocol.costHistoryDays {
            let usd = i < history.count ? history[i] : 0
            let units = (Double(Int((usd * 100).rounded())) / unit).rounded()
            out.append(UInt8(min(max(units, 0), 255)))
        }
    }

    private static func appendU16(_ out: inout Data, _ v: UInt16) {
        out.append(UInt8(v & 0xFF)); out.append(UInt8((v >> 8) & 0xFF))
    }
    private static func appendU32(_ out: inout Data, _ v: UInt32) {
        out.append(UInt8(v & 0xFF)); out.append(UInt8((v >> 8) & 0xFF))
        out.append(UInt8((v >> 16) & 0xFF)); out.append(UInt8((v >> 24) & 0xFF))
    }
    private static func writeU32(_ out: inout Data, _ v: UInt32, at offset: Int) {
        out[offset] = UInt8(v & 0xFF); out[offset + 1] = UInt8((v >> 8) & 0xFF)
        out[offset + 2] = UInt8((v >> 16) & 0xFF); out[offset + 3] = UInt8((v >> 24) & 0xFF)
    }
}
