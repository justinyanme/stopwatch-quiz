// bridge/Sources/StopwatchBridge/CodexbarClient.swift
import Foundation

/// Thin client around `codexbar serve` localhost JSON. Produces `NormalizedUsage`
/// so `SnapshotEncoder` doesn't need to know about the CodexBar wire format.
public actor CodexbarClient {

    public enum Scope: String {
        case all = "all"
        case codex, claude, gemini
    }

    public enum FetchError: Error {
        case http(Int)
        case transport(Error)
        case decode(Error)
    }

    public init(port: UInt16, session: URLSession = .shared, timeout: TimeInterval = 5) {
        self.port = port
        self.session = session
        self.timeout = timeout
    }

    public func fetch(scope: Scope = .all) async throws -> NormalizedUsage {
        var components = URLComponents()
        components.scheme = "http"
        components.host   = "127.0.0.1"
        components.port   = Int(port)
        components.path   = "/usage"
        if scope != .all {
            components.queryItems = [.init(name: "provider", value: scope.rawValue)]
        }
        var req = URLRequest(url: components.url!)
        req.timeoutInterval = timeout

        let data: Data
        let response: URLResponse
        do {
            (data, response) = try await session.data(for: req)
        } catch {
            throw FetchError.transport(error)
        }
        if let http = response as? HTTPURLResponse, http.statusCode != 200 {
            throw FetchError.http(http.statusCode)
        }
        do {
            return try decode(data)
        } catch {
            throw FetchError.decode(error)
        }
    }

    // MARK: - Decoding the CodexBar response shape

    private func decode(_ data: Data) throws -> NormalizedUsage {
        let raw = try JSONDecoder.iso8601.decode(RawResponse.self, from: data)
        var flagBits: SnapshotFlags = []
        if raw.flags?.stale       == true { flagBits.insert(.stale) }
        if raw.flags?.bridgeError == true { flagBits.insert(.bridgeError) }
        if raw.providers.isEmpty           { flagBits.insert(.providerMissing) }

        let providers = raw.providers.compactMap { p -> NormalizedUsage.Provider? in
            guard let id = ProviderID(fromString: p.provider) else { return nil }
            return .init(
                providerID:     id,
                status:         ProviderStatus(fromIndicator: p.status?.indicator),
                sessionPct:     p.usage?.primary?.usedPercent.map { UInt8(clamping: $0) },
                weekPct:        p.usage?.secondary?.usedPercent.map { UInt8(clamping: $0) },
                sessionResetAt: p.usage?.primary?.resetsAt,
                weekResetAt:    p.usage?.secondary?.resetsAt,
                credits:        p.credits?.remaining,
                plan:           ProviderPlan(fromString: p.plan)
            )
        }

        return .init(capturedAt: raw.capturedAt ?? Date(), flags: flagBits, providers: providers)
    }

    // MARK: - Wire shapes (subset of `codexbar serve` JSON we actually use)

    private struct RawResponse: Decodable {
        var capturedAt: Date?
        var flags: Flags?
        var providers: [Provider]

        struct Flags: Decodable { var stale, bridgeError: Bool? }

        struct Provider: Decodable {
            var provider: String
            var status: Status?
            var usage: Usage?
            var credits: Credits?
            var plan: String?

            struct Status: Decodable { var indicator: String? }
            struct Usage: Decodable {
                var primary: Window?
                var secondary: Window?
                struct Window: Decodable { var usedPercent: Int?; var resetsAt: Date? }
            }
            struct Credits: Decodable { var remaining: Double? }
        }
    }

    private let port: UInt16
    private let session: URLSession
    private let timeout: TimeInterval
}

private extension JSONDecoder {
    static let iso8601: JSONDecoder = {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        return d
    }()
}

private extension ProviderID {
    init?(fromString s: String) {
        switch s.lowercased() {
        case "codex":  self = .codex
        case "claude": self = .claude
        case "gemini": self = .gemini
        default:       return nil
        }
    }
}

private extension ProviderStatus {
    init(fromIndicator s: String?) {
        switch (s ?? "").lowercased() {
        case "none":     self = .ok
        case "minor":    self = .warn
        case "major":    self = .critical
        case "critical": self = .critical
        case "":         self = .ok
        default:         self = .ok
        }
    }
}
