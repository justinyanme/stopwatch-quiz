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
