import Foundation
import Testing
@testable import StopwatchBridge

@Suite(.serialized) struct CodexbarClientTests {

    @Test func parsesThreeProvidersFixture() async throws {
        let json = try Fixtures.loadJSON("codexbar-three-providers")
        StubURLProtocol.stub = .init(status: 200, body: json)

        let client = CodexbarClient(port: 51111, session: stubSession())
        let usage = try await client.fetch(scope: .all)

        #expect(usage.providers.count == 3)
        #expect(usage.providers[0].providerID == .codex)
        #expect(usage.providers[0].sessionPct == 72)
        #expect(usage.providers[0].weekPct == 41)
        #expect(usage.providers[0].credits == 112.4)
        #expect(usage.providers[0].plan == .plus)
        #expect(usage.providers[2].weekPct == nil)
        #expect(usage.providers[2].weekResetAt == nil)
    }

    @Test func decodesFractionalUsedPercent() async throws {
        // Regression: real `codexbar serve` returns percentages as fractional
        // Doubles (Gemini e.g. 54.666664999999995). A strict Int decode throws,
        // failing the WHOLE array, so every live fetch was treated as a bridge
        // failure and the watch was stuck showing "stale". usedPercent must decode
        // as a number and round to the nearest whole percent.
        let body = Data("""
        [{"provider":"gemini","usage":{\
        "primary":{"usedPercent":54.666664999999995},\
        "secondary":{"usedPercent":12.4}}}]
        """.utf8)
        StubURLProtocol.stub = .init(status: 200, body: body)

        let client = CodexbarClient(port: 51111, session: stubSession())
        let usage = try await client.fetch(scope: .all)

        #expect(usage.providers.count == 1)
        #expect(usage.providers[0].providerID == .gemini)
        #expect(usage.providers[0].sessionPct == 55)   // 54.666… rounds to 55
        #expect(usage.providers[0].weekPct == 12)       // 12.4 rounds to 12
    }

    @Test func timeoutSurfacesAsError() async {
        StubURLProtocol.stub = .init(error: URLError(.timedOut))
        let client = CodexbarClient(port: 51111, session: stubSession(), timeout: 1)
        await #expect(throws: CodexbarClient.FetchError.self) {
            _ = try await client.fetch(scope: .all)
        }
    }

    @Test func providerScopeReachesURL() async throws {
        let json = try Fixtures.loadJSON("codexbar-codex-only")
        StubURLProtocol.stub = .init(status: 200, body: json)
        StubURLProtocol.captured = []

        let client = CodexbarClient(port: 51111, session: stubSession())
        _ = try await client.fetch(scope: .codex)

        #expect(StubURLProtocol.captured.first?.absoluteString.contains("provider=codex") == true)
    }

    @Test func errorFixtureSurfacesFlags() async throws {
        let json = try Fixtures.loadJSON("codexbar-error")
        StubURLProtocol.stub = .init(status: 200, body: json)

        let client = CodexbarClient(port: 51111, session: stubSession())
        let usage = try await client.fetch(scope: .all)

        #expect(usage.providers.isEmpty)
        #expect(usage.flags.contains(.stale))
        #expect(usage.flags.contains(.bridgeError))
        #expect(usage.flags.contains(.providerMissing))   // set by client because providers.isEmpty
    }

    private func utcDate(_ ymd: String) -> Date {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.timeZone = TimeZone(identifier: "UTC")
        f.dateFormat = "yyyy-MM-dd"
        return f.date(from: ymd)!
    }

    @Test func parsesCostFixture() async throws {
        let json = try Fixtures.loadJSON("codexbar-cost-two")
        StubURLProtocol.stub = .init(status: 200, body: json)

        let client = CodexbarClient(port: 51111, session: stubSession())
        let cost = try await client.fetchCost(scope: .all, now: utcDate("2026-05-29"))

        #expect(cost.providers.count == 2)
        let codex = cost.providers[0]
        #expect(codex.providerID == .codex)
        #expect(codex.todayCostUSD == 12.0)
        #expect(codex.monthCostUSD == 300.0)
        #expect(codex.todayTokens == 1_000_000)
        #expect(codex.monthTokens == 100_000_000)
        #expect(codex.topModel == "gpt-5.5")
        #expect(codex.history.count == 30)
        #expect(codex.history[29] == 120.0)
        #expect(codex.history[27] == 10.0)
        #expect(cost.providers[1].topModel == "claude-opus-4-7")  // full name; encoder shortens
        #expect(cost.providers[1].history[29] == 60.0)
    }

    @Test func emptyCostArraySetsUnavailable() async throws {
        StubURLProtocol.stub = .init(status: 200, body: Data("[]".utf8))
        let client = CodexbarClient(port: 51111, session: stubSession())
        let cost = try await client.fetchCost(scope: .all, now: utcDate("2026-05-29"))
        #expect(cost.providers.isEmpty)
        #expect(cost.flags.contains(.costUnavailable))
    }

    // MARK: - URLProtocol stub plumbing

    private func stubSession() -> URLSession {
        let cfg = URLSessionConfiguration.ephemeral
        cfg.protocolClasses = [StubURLProtocol.self]
        return URLSession(configuration: cfg)
    }
}

final class StubURLProtocol: URLProtocol {
    struct Stub {
        var status: Int = 200
        var body: Data = .init()
        var error: Error? = nil
    }
    nonisolated(unsafe) static var stub: Stub = .init()
    nonisolated(unsafe) static var captured: [URL] = []

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        if let url = request.url { Self.captured.append(url) }
        if let err = Self.stub.error {
            client?.urlProtocol(self, didFailWithError: err)
            return
        }
        let resp = HTTPURLResponse(url: request.url!, statusCode: Self.stub.status,
                                   httpVersion: "HTTP/1.1", headerFields: nil)!
        client?.urlProtocol(self, didReceive: resp, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: Self.stub.body)
        client?.urlProtocolDidFinishLoading(self)
    }

    override func stopLoading() {}
}
