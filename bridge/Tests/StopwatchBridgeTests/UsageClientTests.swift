import Foundation
import Testing
@testable import StopwatchBridge

@Suite(.serialized) struct UsageClientTests {
    private func stubSession() -> URLSession {
        let cfg = URLSessionConfiguration.ephemeral
        cfg.protocolClasses = [BalanceStubURLProtocol.self]
        return URLSession(configuration: cfg)
    }

    @Test func openRouterAggregatesDailyRows() async {
        let body = #"""
        {"data":[
          {"date":"2025-05-28","usage":3.0,"requests":10,"prompt_tokens":100,"completion_tokens":50,"reasoning_tokens":0},
          {"date":"2025-05-28","usage":1.0,"requests":5,"prompt_tokens":20,"completion_tokens":5,"reasoning_tokens":0},
          {"date":"2025-05-27","usage":2.0,"requests":4,"prompt_tokens":10,"completion_tokens":2,"reasoning_tokens":0}
        ]}
        """#
        BalanceStubURLProtocol.routes = ["openrouter.ai": .init(status: 200, body: Data(body.utf8))]
        defer { BalanceStubURLProtocol.routes = [:] }
        let client = UsageClient(keyStore: FakeKeyStore(["openrouter-mgmt": "sk-mgmt"]), session: stubSession())
        let now = ISO8601DateFormatter().date(from: "2025-05-28T12:00:00Z")!
        let snap = await client.fetchAll([.init(kind: .openrouter, credentialID: "openrouter-mgmt")], now: now)

        #expect(snap.providers.count == 1)
        let p = snap.providers[0]
        #expect(p.status == .ok)
        #expect(p.currencyCode == "USD")
        #expect(p.todayCost == 4.0)                 // 3 + 1
        #expect(p.todayRequests == 15)              // 10 + 5
        #expect(p.todayTokens == 175)               // (100+50) + (20+5)
        #expect(p.costHistory[29] == 4.0)           // today is the last bucket
        #expect(p.costHistory[28] == 2.0)           // yesterday
        #expect(p.monthCost == 6.0)                 // sum of all days
    }

    @Test func missingManagementKeyIsAuthError() async {
        let client = UsageClient(keyStore: FakeKeyStore(), session: stubSession())
        let snap = await client.fetchAll([.init(kind: .openrouter, credentialID: "openrouter-mgmt")],
                                         now: Date(timeIntervalSince1970: 100))
        #expect(snap.providers[0].status == .authError)
    }

    @Test func http401IsAuthError() async {
        BalanceStubURLProtocol.routes = ["openrouter.ai": .init(status: 401, body: Data())]
        defer { BalanceStubURLProtocol.routes = [:] }
        let client = UsageClient(keyStore: FakeKeyStore(["openrouter-mgmt": "bad"]), session: stubSession())
        let snap = await client.fetchAll([.init(kind: .openrouter, credentialID: "openrouter-mgmt")],
                                         now: Date(timeIntervalSince1970: 100))
        #expect(snap.providers[0].status == .authError)
    }
}
