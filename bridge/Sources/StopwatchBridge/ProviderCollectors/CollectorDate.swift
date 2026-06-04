import Foundation

/// Lenient ISO-8601 parsing for provider usage timestamps.
///
/// Provider usage APIs return reset timestamps in both fractional-second
/// (`"2026-06-02T12:00:00.123Z"`) and plain (`"2026-06-02T12:00:00Z"`) forms.
/// `JSONDecoder`'s `.iso8601` strategy only accepts the plain form, so a present
/// fractional timestamp throws and fails the *entire* response decode — silently
/// dropping the whole provider via the collectors' `try?`. Decoding the
/// timestamps as strings and parsing them here keeps a stray timestamp format
/// from taking down the numbers we actually render.
///
/// Mirrors the box+lock idiom the vendored cost scanner uses to keep a
/// non-`Sendable` `ISO8601DateFormatter` Swift 6 concurrency-safe.
private final class CollectorISO8601Box: @unchecked Sendable {
    let lock = NSLock()
    let withFractional: ISO8601DateFormatter = {
        let f = ISO8601DateFormatter()
        f.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return f
    }()
    let plain: ISO8601DateFormatter = {
        let f = ISO8601DateFormatter()
        f.formatOptions = [.withInternetDateTime]
        return f
    }()
}

enum CollectorDate {
    private static let box = CollectorISO8601Box()

    /// Parses fractional or plain ISO-8601. Returns `nil` for nil/empty/unparseable input.
    static func parseISO8601(_ text: String?) -> Date? {
        guard let text, !text.isEmpty else { return nil }
        box.lock.lock()
        defer { box.lock.unlock() }
        return box.withFractional.date(from: text) ?? box.plain.date(from: text)
    }
}
