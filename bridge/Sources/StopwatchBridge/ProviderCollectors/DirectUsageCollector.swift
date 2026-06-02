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
        if let codexProvider = try? await codex.fetch(now: now) { providers.append(codexProvider) }
        if let claudeProvider = try? await claude.fetch() { providers.append(claudeProvider) }
        if let geminiProvider = try? await gemini.fetch() { providers.append(geminiProvider) }
        var flags: SnapshotFlags = []
        if providers.isEmpty { flags.insert(.providerMissing) }
        return NormalizedUsage(capturedAt: now, flags: flags, providers: providers)
    }
}
