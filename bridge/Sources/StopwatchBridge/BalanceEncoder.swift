// bridge/Sources/StopwatchBridge/BalanceEncoder.swift
import Foundation

public enum BalanceEncoder {

    /// Encodes normalized balances into the `8 + 36*N` byte BalanceSnapshot wire
    /// format. See `shared/PROTOCOL.md §3B`. Clamps to `balanceMaxRecords`.
    public static func encode(_ snap: NormalizedBalance) -> Data {
        let providers = Array(snap.providers.prefix(Protocol.balanceMaxRecords))
        if providers.count < snap.providers.count {
            FileHandle.standardError.write(Data("balance: \(snap.providers.count) providers > max \(Protocol.balanceMaxRecords); truncating\n".utf8))
        }

        var out = Data(capacity: Protocol.balanceHeaderSize + Protocol.balanceRecordSize * providers.count)
        out.append(Protocol.balanceVersionMajor)
        out.append(Protocol.balanceVersionMinor)
        out.append(UInt8(providers.count))
        out.append(snap.flags.rawValue)
        appendU32(&out, u32Seconds(snap.capturedAt))

        for p in providers {
            out.append(p.kind.rawValue)
            out.append(p.status.rawValue)
            out.append(p.isLow ? Protocol.balanceRecordFlagLow : 0)
            appendCurrency(&out, p.currencyCode)
            out.append(UInt8(max(0, min(p.currencyDecimals, 255))))
            out.append(0)  // reserved
            appendU32(&out, minorOrSentinel(p.remaining, decimals: p.currencyDecimals, unlimited: p.unlimited))
            appendU32(&out, minorOrSentinel(p.usage, decimals: p.currencyDecimals, unlimited: false))
            appendU32(&out, p.updatedAt.map(u32Seconds) ?? 0)
            appendName(&out, p.name)
        }
        return out
    }

    public static func staleEmpty() -> Data {
        encode(NormalizedBalance(capturedAt: Date(timeIntervalSince1970: 0),
                                 flags: [.stale], providers: []))
    }

    public static func errorEmpty(capturedAt: Date = Date()) -> Data {
        encode(NormalizedBalance(capturedAt: capturedAt,
                                 flags: [.stale, .bridgeError], providers: []))
    }

    /// Sets `stale` (plus `extraFlags`) and refreshes capturedAt on an encoded snapshot.
    public static func markStale(_ snapshot: Data, capturedAt: Date, extraFlags: BalanceFlags = []) -> Data {
        guard snapshot.count >= Protocol.balanceHeaderSize else { return snapshot }
        var out = snapshot
        out[3] |= BalanceFlags.stale.rawValue | extraFlags.rawValue
        writeU32(&out, u32Seconds(capturedAt), at: 4)
        return out
    }

    // MARK: - helpers

    private static let balanceUnlimited: UInt32 = 0xFFFF_FFFE
    private static let balanceUnknown:   UInt32 = 0xFFFF_FFFF

    private static func minorOrSentinel(_ value: Double?, decimals: Int, unlimited: Bool) -> UInt32 {
        if unlimited { return balanceUnlimited }
        guard let value else { return balanceUnknown }
        let scale = pow(10.0, Double(max(0, decimals)))
        let minor = (value * scale).rounded()
        if minor < 0 { return 0 }
        if minor >= Double(balanceUnlimited) { return balanceUnlimited - 1 }  // clamp below sentinels
        return UInt32(minor)
    }

    private static func appendCurrency(_ out: inout Data, _ code: String) {
        var field = [UInt8](repeating: 0, count: 3)
        for (i, b) in code.uppercased().utf8.prefix(3).enumerated() { field[i] = b }
        out.append(contentsOf: field)
    }

    private static func appendName(_ out: inout Data, _ name: String) {
        var field = [UInt8](repeating: 0, count: 16)
        for (i, b) in name.utf8.prefix(15).enumerated() { field[i] = b }
        out.append(contentsOf: field)
    }

    private static func u32Seconds(_ d: Date) -> UInt32 { UInt32(max(0, d.timeIntervalSince1970)) }

    private static func appendU32(_ out: inout Data, _ v: UInt32) {
        out.append(UInt8(v & 0xFF)); out.append(UInt8((v >> 8) & 0xFF))
        out.append(UInt8((v >> 16) & 0xFF)); out.append(UInt8((v >> 24) & 0xFF))
    }
    private static func writeU32(_ out: inout Data, _ v: UInt32, at o: Int) {
        out[o] = UInt8(v & 0xFF); out[o+1] = UInt8((v >> 8) & 0xFF)
        out[o+2] = UInt8((v >> 16) & 0xFF); out[o+3] = UInt8((v >> 24) & 0xFF)
    }
}
