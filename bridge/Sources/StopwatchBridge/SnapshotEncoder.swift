// bridge/Sources/StopwatchBridge/SnapshotEncoder.swift
import Foundation

public enum SnapshotEncoder {

    /// Encodes the normalized usage into the 56-byte v1.0 wire format.
    /// See `shared/PROTOCOL.md` §3 for the layout.
    public static func encode(_ usage: NormalizedUsage) -> Data {
        var out = Data(capacity: Protocol.snapshotSize)

        // --- Header (8 bytes) ---
        out.append(Protocol.versionMajor)
        out.append(Protocol.versionMinor)
        precondition(usage.providers.count <= 255, "providers.count \(usage.providers.count) exceeds UInt8 range")
        out.append(UInt8(usage.providers.count))
        out.append(usage.flags.rawValue)
        appendU32(&out, UInt32(max(0, usage.capturedAt.timeIntervalSince1970)))

        // --- Per-provider records (16 bytes each, in input order) ---
        for p in usage.providers {
            out.append(p.providerID.rawValue)
            out.append(p.status.rawValue)
            out.append(p.sessionPct ?? 0xFF)
            out.append(p.weekPct ?? 0xFF)
            appendU32(&out, p.sessionResetAt.map { UInt32(max(0, $0.timeIntervalSince1970)) } ?? 0)
            appendU32(&out, p.weekResetAt.map    { UInt32(max(0, $0.timeIntervalSince1970)) } ?? 0)
            appendU16(&out, p.credits.map { UInt16(max(0.0, min(($0 * 10).rounded(), 65534))) } ?? 0xFFFF)
            out.append(p.plan.rawValue)
            out.append(0)  // reserved
        }
        return out
    }

    private static func appendU16(_ out: inout Data, _ v: UInt16) {
        out.append(UInt8(v & 0xFF))
        out.append(UInt8((v >> 8) & 0xFF))
    }

    private static func appendU32(_ out: inout Data, _ v: UInt32) {
        out.append(UInt8(v & 0xFF))
        out.append(UInt8((v >>  8) & 0xFF))
        out.append(UInt8((v >> 16) & 0xFF))
        out.append(UInt8((v >> 24) & 0xFF))
    }

    /// Returns a valid 56-byte v1.0 snapshot showing all three providers as disabled
    /// with `stale` flag set. Used as the initial GATT characteristic value so a watch
    /// reading before the first real snapshot lands sees a well-formed (but flagged) frame
    /// instead of zero bytes that look like versionMajor=0.
    public static func staleEmpty() -> Data {
        encode(NormalizedUsage(
            capturedAt: Date(timeIntervalSince1970: 0),
            flags: [.stale],
            providers: [
                .init(providerID: .codex,  status: .disabled, sessionPct: nil, weekPct: nil,
                      sessionResetAt: nil, weekResetAt: nil, credits: nil, plan: .unknown),
                .init(providerID: .claude, status: .disabled, sessionPct: nil, weekPct: nil,
                      sessionResetAt: nil, weekResetAt: nil, credits: nil, plan: .unknown),
                .init(providerID: .gemini, status: .disabled, sessionPct: nil, weekPct: nil,
                      sessionResetAt: nil, weekResetAt: nil, credits: nil, plan: .unknown),
            ]
        ))
    }
}
