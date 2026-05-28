import Foundation

enum DecodeCommand {
    /// Decodes a hex string (from a BLE capture) into readable JSON.
    /// Used for debugging: `stopwatch-bridge decode-snapshot 010003000ea2..`.
    static func run(_ hex: String) -> Int32 {
        let cleaned = hex.filter { !$0.isWhitespace }
        guard cleaned.count % 2 == 0 else {
            FileHandle.standardError.write(Data("hex string has odd character count (\(cleaned.count))\n".utf8))
            return 2
        }
        var bytes = [UInt8]()
        var i = cleaned.startIndex
        while i < cleaned.endIndex {
            let next = cleaned.index(i, offsetBy: 2, limitedBy: cleaned.endIndex) ?? cleaned.endIndex
            guard let b = UInt8(cleaned[i..<next], radix: 16) else {
                FileHandle.standardError.write(Data("bad hex at offset \(cleaned.distance(from: cleaned.startIndex, to: i))\n".utf8))
                return 2
            }
            bytes.append(b)
            i = next
        }
        guard bytes.count >= Protocol.headerSize else {
            FileHandle.standardError.write(Data("payload too short: \(bytes.count) bytes\n".utf8))
            return 2
        }
        let major = bytes[0], minor = bytes[1], count = bytes[2], flags = bytes[3]
        let capturedAt = readU32(bytes, 4)
        var out: [String: Any] = [
            "versionMajor": Int(major),
            "versionMinor": Int(minor),
            "providerCount": Int(count),
            "flags": [
                "stale":           (flags & 0x01) != 0,
                "bridgeError":     (flags & 0x02) != 0,
                "providerMissing": (flags & 0x04) != 0,
            ],
            "capturedAt": Date(timeIntervalSince1970: TimeInterval(capturedAt)).description,
        ]
        var providers: [[String: Any]] = []
        var off = Protocol.headerSize
        for _ in 0..<Int(count) {
            guard off + Protocol.perProviderSize <= bytes.count else { break }
            let pid = bytes[off]
            providers.append([
                "providerID": Int(pid),
                "status":     Int(bytes[off + 1]),
                "sessionPct": bytes[off + 2] == 0xFF ? "unknown" : Int(bytes[off + 2]).description,
                "weekPct":    bytes[off + 3] == 0xFF ? "unknown" : Int(bytes[off + 3]).description,
                "sessionResetAt": dateOrUnknown(readU32(bytes, off + 4)),
                "weekResetAt":    dateOrUnknown(readU32(bytes, off + 8)),
                "credits":    readU16(bytes, off + 12) == 0xFFFF ? "unknown" : Double(readU16(bytes, off + 12)) / 10,
                "plan":       Int(bytes[off + 14]),
            ])
            off += Protocol.perProviderSize
        }
        out["providers"] = providers
        let data = try! JSONSerialization.data(withJSONObject: out, options: [.prettyPrinted, .sortedKeys])
        FileHandle.standardOutput.write(data)
        FileHandle.standardOutput.write(Data("\n".utf8))
        return 0
    }

    private static func readU16(_ b: [UInt8], _ off: Int) -> UInt16 {
        UInt16(b[off]) | (UInt16(b[off + 1]) << 8)
    }
    private static func readU32(_ b: [UInt8], _ off: Int) -> UInt32 {
        UInt32(b[off]) | (UInt32(b[off + 1]) << 8) | (UInt32(b[off + 2]) << 16) | (UInt32(b[off + 3]) << 24)
    }
    private static func dateOrUnknown(_ t: UInt32) -> String {
        t == 0 ? "unknown" : Date(timeIntervalSince1970: TimeInterval(t)).description
    }
}
