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

    @Test func scaleConvertsRawUnitsToCurrency() async {
        // AiHubMix: GET /api/user/self → {"data":{"quota":N}}, raw auth, 500000 quota = $1.
        BalanceStubURLProtocol.routes = [
            "aihubmix.com": .init(status: 200, body: Data(#"{"data":{"quota":2500000}}"#.utf8))
        ]
        var pc = ProviderConfig(id: "ah", name: "AiHubMix", kind: "generic")
        pc.endpoint = "https://aihubmix.com/api/user/self"
        pc.balancePath = "data.quota"; pc.currency = "USD"; pc.scale = 500000; pc.auth = "raw"
        let p = await BalanceClient(keyStore: FakeKeyStore(["ah": "fd-tok"]), session: stubSession())
            .fetchAll([pc.resolved()], now: .init(timeIntervalSince1970: 100)).providers[0]
        #expect(p.status == .ok)
        #expect(p.remaining == 5.0)        // 2,500,000 quota / 500,000 = $5
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

@Suite struct BalanceCacheTests {
    private func ok(_ id: String, _ remaining: Double) -> NormalizedBalance.Provider {
        .init(kind: .generic, name: id, status: .ok, currencyCode: "USD",
              remaining: remaining, updatedAt: Date(timeIntervalSince1970: 100))
    }
    private func failed(_ id: String) -> NormalizedBalance.Provider {
        .init(kind: .generic, name: id, status: .unreachable, currencyCode: "", remaining: nil, updatedAt: nil)
    }

    @Test func retainsLastGoodForFailedProvider() {
        var cache = BalanceCache()
        _ = cache.record(NormalizedBalance(capturedAt: Date(timeIntervalSince1970: 100), flags: [],
                                           providers: [ok("a", 10), ok("b", 20)]))
        let merged = cache.record(NormalizedBalance(capturedAt: Date(timeIntervalSince1970: 200), flags: [],
                                                    providers: [failed("a"), ok("b", 15)]))
        let snap = Snapshot_decodeBalances(merged)
        #expect(snap.count == 2)
        let a = snap.first { $0.name == "a" }!
        #expect(a.status == BalanceStatus.stale.rawValue)   // last-good kept, marked stale
        #expect(a.balanceMinor == 1000)                     // $10.00 retained
        let b = snap.first { $0.name == "b" }!
        #expect(b.balanceMinor == 1500)                     // fresh $15.00
    }
}

struct DecodedBalance { var name: String; var status: UInt8; var balanceMinor: UInt32 }
func Snapshot_decodeBalances(_ data: Data) -> [DecodedBalance] {
    let b = [UInt8](data); let count = Int(b[2]); var out: [DecodedBalance] = []
    for i in 0..<count {
        let o = 8 + 36 * i
        let name = String(decoding: b[o+20..<o+36].prefix { $0 != 0 }, as: UTF8.self)
        let bal = UInt32(b[o+8]) | UInt32(b[o+9])<<8 | UInt32(b[o+10])<<16 | UInt32(b[o+11])<<24
        out.append(.init(name: name, status: b[o+1], balanceMinor: bal))
    }
    return out
}
