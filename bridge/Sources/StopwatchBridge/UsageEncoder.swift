// bridge/Sources/StopwatchBridge/UsageEncoder.swift
import Foundation

public enum UsageEncoder {

    /// Encodes normalized usage into the `12 + 96*N` byte BalanceUsage wire format
    /// (see `shared/PROTOCOL.md §3C`). Clamps to `usageMaxRecords`.
    public static func encode(_ snap: NormalizedUsageSpend) -> Data {
        let providers = Array(snap.providers.prefix(Protocol.usageMaxRecords))
        var out = Data(capacity: Protocol.usageHeaderSize + Protocol.usageRecordSize * providers.count)
        out.append(Protocol.usageVersionMajor)
        out.append(Protocol.usageVersionMinor)
        out.append(UInt8(providers.count))
        out.append(snap.flags.rawValue)
        appendU32(&out, u32Seconds(snap.capturedAt))
        out.append(UInt8(Protocol.usageHistoryDays))
        out.append(0)              // reserved
        appendU16(&out, 0)         // reserved (per-record scales)

        for p in providers {
            let costMinors = p.costHistory.map { minor($0, p.currencyDecimals) }
            let costUnit  = unitU16(costMinors)
            let tokenUnit = unitU32(p.tokenHistory)
            out.append(p.kind.rawValue)
            out.append(p.status.rawValue)
            appendCurrency(&out, p.currencyCode)
            out.append(UInt8(max(0, min(p.currencyDecimals, 255))))
            appendU16(&out, costUnit)
            appendU32(&out, tokenUnit)
            appendU32(&out, minorOrUnknown(p.todayCost, p.currencyDecimals))
            appendU32(&out, minorOrUnknown(p.monthCost, p.currencyDecimals))
            appendU32(&out, u32OrUnknown(p.todayTokens))
            appendU32(&out, u32OrUnknown(p.monthTokens))
            appendU32(&out, u32OrUnknown(p.todayRequests))
            appendU32(&out, u32OrUnknown(p.monthRequests))
            appendScaled(&out, costMinors.map { UInt64(max(0, $0)) }, unit: UInt64(costUnit))
            appendScaled(&out, p.tokenHistory, unit: UInt64(tokenUnit))
        }
        return out
    }

    public static func staleEmpty() -> Data {
        encode(.init(capturedAt: Date(timeIntervalSince1970: 0), flags: [.stale, .unavailable], providers: []))
    }
    public static func errorEmpty(capturedAt: Date = Date()) -> Data {
        encode(.init(capturedAt: capturedAt, flags: [.stale, .bridgeError], providers: []))
    }
    /// Sets stale (+extra flags) and refreshes capturedAt on an encoded snapshot.
    public static func markStale(_ snapshot: Data, capturedAt: Date, extraFlags: UsageFlags = []) -> Data {
        guard snapshot.count >= Protocol.usageHeaderSize else { return snapshot }
        var out = snapshot
        out[3] |= UsageFlags.stale.rawValue | extraFlags.rawValue
        writeU32(&out, u32Seconds(capturedAt), at: 4)
        return out
    }

    // MARK: - helpers
    private static func minor(_ v: Double, _ decimals: Int) -> Int {
        Int((v * pow(10.0, Double(max(0, decimals)))).rounded())
    }
    private static func unitU16(_ values: [Int]) -> UInt16 {
        let maxV = values.max() ?? 0
        if maxV <= 0 { return 1 }
        return UInt16(min(max((maxV + 254) / 255, 1), 65535))
    }
    private static func unitU32(_ values: [UInt64]) -> UInt32 {
        let maxV = values.max() ?? 0
        if maxV == 0 { return 1 }
        return UInt32(min(max((maxV + 254) / 255, 1), UInt64(UInt32.max)))
    }
    private static func minorOrUnknown(_ v: Double?, _ decimals: Int) -> UInt32 {
        guard let v else { return 0xFFFF_FFFF }
        let m = minor(v, decimals)
        if m < 0 { return 0 }
        if m >= Int(UInt32.max) { return 0xFFFF_FFFE }
        return UInt32(m)
    }
    private static func u32OrUnknown(_ v: UInt64?) -> UInt32 {
        guard let v else { return 0xFFFF_FFFF }
        return v >= UInt64(UInt32.max) ? 0xFFFF_FFFE : UInt32(v)
    }
    private static func appendScaled(_ out: inout Data, _ values: [UInt64], unit: UInt64) {
        let u = max(unit, 1)
        for i in 0..<Protocol.usageHistoryDays {
            let v = i < values.count ? values[i] : 0
            out.append(UInt8(min((v + u/2) / u, 255)))
        }
    }
    private static func appendCurrency(_ out: inout Data, _ code: String) {
        var f = [UInt8](repeating: 0, count: 3)
        for (i, b) in code.uppercased().utf8.prefix(3).enumerated() { f[i] = b }
        out.append(contentsOf: f)
    }
    private static func u32Seconds(_ d: Date) -> UInt32 { UInt32(max(0, d.timeIntervalSince1970)) }
    private static func appendU16(_ out: inout Data, _ v: UInt16) {
        out.append(UInt8(v & 0xFF)); out.append(UInt8((v >> 8) & 0xFF))
    }
    private static func appendU32(_ out: inout Data, _ v: UInt32) {
        out.append(UInt8(v & 0xFF)); out.append(UInt8((v >> 8) & 0xFF))
        out.append(UInt8((v >> 16) & 0xFF)); out.append(UInt8((v >> 24) & 0xFF))
    }
    private static func writeU32(_ out: inout Data, _ v: UInt32, at o: Int) {
        out[o] = UInt8(v & 0xFF); out[o+1] = UInt8((v >> 8) & 0xFF)
        out[o+2] = UInt8((v >> 16) & 0xFF); out[o+3] = UInt8((v >> 24) & 0xFF)
    }
}
