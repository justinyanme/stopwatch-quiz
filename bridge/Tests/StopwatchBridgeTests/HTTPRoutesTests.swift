import Testing
@testable import StopwatchBridge

@Suite struct HTTPRoutesTests {
    @Test func parsesSnapshotRoutes() throws {
        #expect(try HTTPRoute.parse(method: "GET", path: "/v1/snapshot", query: [:]) == .read(.snapshot))
        #expect(try HTTPRoute.parse(method: "GET", path: "/v1/cost", query: [:]) == .read(.cost))
        #expect(try HTTPRoute.parse(method: "GET", path: "/v1/balances", query: [:]) == .read(.balances))
        #expect(try HTTPRoute.parse(method: "GET", path: "/v1/balance-usage", query: [:]) == .read(.balanceUsage))
    }

    @Test func parsesHealthAndRefresh() throws {
        #expect(try HTTPRoute.parse(method: "GET", path: "/v1/health", query: [:]) == .health)
        #expect(try HTTPRoute.parse(method: "POST", path: "/v1/refresh", query: ["scope": "4"]) == .refresh(4))
    }

    @Test func rejectsBadRoutes() {
        #expect(throws: HTTPRouteError.notFound) {
            try HTTPRoute.parse(method: "GET", path: "/unknown", query: [:])
        }
        #expect(throws: HTTPRouteError.methodNotAllowed) {
            try HTTPRoute.parse(method: "POST", path: "/v1/snapshot", query: [:])
        }
        #expect(throws: HTTPRouteError.badRequest) {
            try HTTPRoute.parse(method: "POST", path: "/v1/refresh", query: ["scope": "999"])
        }
    }
}
