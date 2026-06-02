import Foundation

// Minimal shims so the vendored CodexBar cost-scanner subset compiles in this
// target without dragging in CodexBar's logging, generated parser-hash, or
// Bedrock provider stack. None of these affect the Codex/Claude cost paths the
// bridge actually uses.

// MARK: - Logging

/// No-op stand-in for CodexBar's `CodexBarLogger`. The vendored scanners only
/// ever call `.warning(_:metadata:)`; we discard those diagnostics.
struct CostUsageLogger: Sendable {
    func warning(_ message: String, metadata: [String: String] = [:]) {}
}

enum CodexBarLog {
    static func logger(_ category: String) -> CostUsageLogger { CostUsageLogger() }
}

enum LogCategories {
    static let tokenCost = "token-cost"
}

// MARK: - Parser hash

/// CodexBar generates this hash to invalidate the Codex log cache when the
/// parser changes. A stable constant is sufficient here; the vendored cache
/// lives under our own root.
enum CodexParserHash {
    static let value = "stopwatch-vendored"
}

// MARK: - Bedrock (compile-only)

/// The bridge collects Codex and Claude cost only, so the Bedrock branch in
/// `CostUsageFetcher` is never executed. These stubs exist solely to satisfy the
/// compiler; they throw if ever reached.
struct BedrockCredentials: Sendable {}

enum BedrockCredentialResolver {
    struct Resolved: Sendable { let credentials: BedrockCredentials }
    static func resolve(environment: [String: String]) async throws -> Resolved {
        throw DirectCollectorError.unavailable
    }
}

enum BedrockUsageFetcher {
    static func fetchDailyReport(credentials: BedrockCredentials,
                                 since: Date,
                                 until: Date,
                                 environment: [String: String]) async throws -> CostUsageDailyReport {
        throw DirectCollectorError.unavailable
    }
}
