import Foundation

public actor DirectUsageCollector {
    private let codex: CodexUsageCollector
    private let gemini: GeminiUsageCollector

    public init(codex: CodexUsageCollector = .init(), gemini: GeminiUsageCollector = .init()) {
        self.codex = codex
        self.gemini = gemini
    }

    public func fetchAll(now: Date = Date()) async -> NormalizedUsage {
        var providers: [NormalizedUsage.Provider] = []
        if let codexProvider = try? await codex.fetch(now: now) { providers.append(codexProvider) }
        if let geminiProvider = try? await gemini.fetch() { providers.append(geminiProvider) }
        var flags: SnapshotFlags = []
        if providers.isEmpty { flags.insert(.providerMissing) }
        return NormalizedUsage(capturedAt: now, flags: flags, providers: providers)
    }
}
