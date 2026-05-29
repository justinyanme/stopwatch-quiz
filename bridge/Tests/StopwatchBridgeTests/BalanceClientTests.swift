import Foundation
import Testing
@testable import StopwatchBridge

@Suite(.serialized) struct BalanceClientTests {

    private func stubSession() -> URLSession {
        let cfg = URLSessionConfiguration.ephemeral
        cfg.protocolClasses = [BalanceStubURLProtocol.self]
        return URLSession(configuration: cfg)
    }

    private func cfg(_ id: String, kind: String, low: Double? = nil) -> ProviderConfig.Resolved {
        var c = ProviderConfig(id: id, name: id.capitalized, kind: kind)
        c.lowThreshold = low
        return c.resolved()
    }

    @Test func openRouterRemainingIsCreditsMinusUsage() async {
        BalanceStubURLProtocol.routes = [
            "openrouter.ai": .init(status: 200, body: Data(#"{"data":{"total_credits":50.0,"total_usage":7.9}}"#.utf8))
        ]
        let client = BalanceClient(keyStore: FakeKeyStore(["or": "sk-x"]), session: stubSession())
        let snap = await client.fetchAll([cfg("or", kind: "openrouter", low: 5.0)], now: .init(timeIntervalSince1970: 100))
        #expect(snap.providers.count == 1)
        let p = snap.providers[0]
        #expect(p.status == .ok)
        #expect(p.remaining == 42.1)             // 50 - 7.9
        #expect(p.usage == 7.9)
        #expect(p.currencyCode == "USD")
        #expect(p.isLow == false)                // 42.1 >= 5
    }

    @Test func deepSeekParsesStringBalanceAndInBodyCurrency() async {
        BalanceStubURLProtocol.routes = [
            "api.deepseek.com": .init(status: 200, body: Data(#"{"is_available":true,"balance_infos":[{"currency":"CNY","total_balance":"318.50"}]}"#.utf8))
        ]
        let client = BalanceClient(keyStore: FakeKeyStore(["ds": "sk-y"]), session: stubSession())
        let p = await client.fetchAll([cfg("ds", kind: "deepseek")], now: .init(timeIntervalSince1970: 100)).providers[0]
        #expect(p.remaining == 318.5)
        #expect(p.currencyCode == "CNY")
        #expect(p.status == .ok)
    }

    @Test func missingKeyIsAuthError() async {
        let client = BalanceClient(keyStore: FakeKeyStore(), session: stubSession())
        let p = await client.fetchAll([cfg("or", kind: "openrouter")], now: .init(timeIntervalSince1970: 100)).providers[0]
        #expect(p.status == .authError)
        #expect(p.remaining == nil)
    }

    @Test func http401IsAuthError_402IsDepleted_timeoutUnreachable() async {
        BalanceStubURLProtocol.routes = ["api.deepseek.com": .init(status: 401, body: Data())]
        let c = BalanceClient(keyStore: FakeKeyStore(["ds": "k"]), session: stubSession())
        #expect(await c.fetchAll([cfg("ds", kind: "deepseek")], now: .init()).providers[0].status == .authError)

        BalanceStubURLProtocol.routes = ["api.deepseek.com": .init(status: 402, body: Data())]
        #expect(await c.fetchAll([cfg("ds", kind: "deepseek")], now: .init()).providers[0].status == .depleted)

        BalanceStubURLProtocol.routes = ["api.deepseek.com": .init(error: URLError(.timedOut))]
        #expect(await c.fetchAll([cfg("ds", kind: "deepseek")], now: .init()).providers[0].status == .unreachable)
    }

    @Test func lowThresholdFlagsBalance() async {
        BalanceStubURLProtocol.routes = [
            "openrouter.ai": .init(status: 200, body: Data(#"{"data":{"total_credits":4.0,"total_usage":1.0}}"#.utf8))
        ]
        let c = BalanceClient(keyStore: FakeKeyStore(["or": "k"]), session: stubSession())
        let p = await c.fetchAll([cfg("or", kind: "openrouter", low: 5.0)], now: .init()).providers[0]
        #expect(p.remaining == 3.0)
        #expect(p.isLow == true)                 // 3 < 5
    }

    @Test func dottedPathEvaluator() {
        let obj = try! JSONSerialization.jsonObject(with: Data(#"{"a":{"b":[{"c":9}]},"d":"5.5"}"#.utf8))
        #expect(BalanceClient.value(at: "a.b[0].c", in: obj) as? Int == 9)
        #expect(BalanceClient.value(at: "d", in: obj) as? String == "5.5")
        #expect(BalanceClient.value(at: "a.missing", in: obj) == nil)
        #expect(BalanceClient.value(at: "a.b[3].c", in: obj) == nil)   // out of range
    }
}

final class BalanceStubURLProtocol: URLProtocol {
    struct Route { var status: Int = 200; var body: Data = .init(); var error: Error? = nil }
    nonisolated(unsafe) static var routes: [String: Route] = [:]   // keyed by url host substring

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        let host = request.url?.host ?? ""
        let route = Self.routes.first { host.contains($0.key) }?.value ?? Route(status: 404)
        if let err = route.error { client?.urlProtocol(self, didFailWithError: err); return }
        let resp = HTTPURLResponse(url: request.url!, statusCode: route.status, httpVersion: "HTTP/1.1", headerFields: nil)!
        client?.urlProtocol(self, didReceive: resp, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: route.body)
        client?.urlProtocolDidFinishLoading(self)
    }
    override func stopLoading() {}
}
