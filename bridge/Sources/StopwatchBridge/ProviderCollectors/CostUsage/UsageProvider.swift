import Foundation

/// Local provider enum for the vendored cost scanners. CodexBar's full provider
/// registry pulls in UI/cookie dependencies we do not want, so we vendor only
/// this bare enum. The vendored scanners `switch` exhaustively over it, so the
/// full case list is replicated verbatim from CodexBar; raw values match so any
/// on-disk cache keys stay compatible. The bridge only ever drives `.codex` and
/// `.claude`.
public enum UsageProvider: String, CaseIterable, Sendable, Codable {
    case codex
    case openai
    case azureopenai
    case claude
    case cursor
    case opencode
    case opencodego
    case alibaba
    case alibabatokenplan
    case factory
    case gemini
    case antigravity
    case copilot
    case zai
    case minimax
    case manus
    case kimi
    case kilo
    case kiro
    case vertexai
    case augment
    case jetbrains
    case kimik2
    case moonshot
    case amp
    case t3chat
    case ollama
    case synthetic
    case warp
    case openrouter
    case elevenlabs
    case windsurf
    case perplexity
    case mimo
    case doubao
    case abacus
    case mistral
    case deepseek
    case codebuff
    case crof
    case venice
    case commandcode
    case stepfun
    case bedrock
    case grok
    case groq
    case llmproxy
    case deepgram
}
