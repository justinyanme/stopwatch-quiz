import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct ProvidersConfigTests {

    @Test func decodesAndAppliesKindDefaults() throws {
        let json = Data("""
        [ { "id": "openrouter", "name": "OpenRouter", "kind": "openrouter", "lowThreshold": 5.0 } ]
        """.utf8)
        let providers = try ProvidersConfig.decode(json)
        #expect(providers.count == 1)
        let p = providers[0].resolved()      // kind defaults filled in
        #expect(p.kind == .openrouter)
        #expect(p.endpoint == "https://openrouter.ai/api/v1/credits")
        #expect(p.balancePath == "data.total_credits")
        #expect(p.usagePath == "data.total_usage")
        #expect(p.currency == "USD")
        #expect(p.pollSeconds == 900)
        #expect(p.lowThreshold == 5.0)
    }

    @Test func genericRequiresExplicitFields() throws {
        let json = Data("""
        [ { "id": "x", "name": "X", "kind": "generic",
            "endpoint": "https://api.x.com/bal", "balancePath": "data.balance", "currency": "USD" } ]
        """.utf8)
        let p = try ProvidersConfig.decode(json)[0].resolved()
        #expect(p.endpoint == "https://api.x.com/bal")
        #expect(p.usagePath == nil)
        #expect(p.currencyDecimals == 2)
    }

    @Test func missingFileYieldsEmpty() throws {
        let url = URL(fileURLWithPath: "/tmp/does-not-exist-\(UUID()).json")
        #expect(try ProvidersConfig.load(from: url).isEmpty)
    }
}
