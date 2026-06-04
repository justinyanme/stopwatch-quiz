import Foundation

public actor DirectUsageCollector {
    private let codex: CodexUsageCollector
    private let claude: ClaudeUsageCollector
    private let gemini: GeminiUsageCollector

    public init(
        codex: CodexUsageCollector = .init(),
        claude: ClaudeUsageCollector = .init(),
        gemini: GeminiUsageCollector = .init())
    {
        self.codex = codex
        self.claude = claude
        self.gemini = gemini
    }

    public func fetchAll(now: Date = Date()) async -> NormalizedUsage {
        var providers: [NormalizedUsage.Provider] = []
        do { providers.append(try await codex.fetch(now: now)) } catch { Self.log("codex", error) }
        do { providers.append(try await claude.fetch()) } catch { Self.log("claude", error) }
        do { providers.append(try await gemini.fetch()) } catch { Self.log("gemini", error) }
        var flags: SnapshotFlags = []
        if providers.isEmpty { flags.insert(.providerMissing) }
        return NormalizedUsage(capturedAt: now, flags: flags, providers: providers)
    }

    /// Surfaces why a provider was dropped (auth, HTTP status, decode) instead of
    /// silently swallowing it — the watch otherwise just shows "no source".
    private static func log(_ provider: String, _ error: Error) {
        FileHandle.standardError.write(Data("usage \(provider) unavailable: \(error)\n".utf8))
    }
}
