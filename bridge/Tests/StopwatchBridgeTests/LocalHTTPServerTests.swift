import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct HTTPHandlerTests {
    @Test func returnsBinarySnapshotForAuthorizedRequest() async {
        let repo = SnapshotRepository()
        let bytes = SnapshotEncoder.encodeGATTSnapshot(.threeProvidersFixture)
        await repo.update(.snapshot, bytes: bytes)
        let scheduler = RefreshScheduler { _ in }
        let handler = SnapshotHTTPHandler(
            repository: repo,
            authenticator: HTTPAuthenticator(apiToken: "secret"),
            scheduler: scheduler)

        let response = await handler.handle(.init(method: "GET", path: "/v1/snapshot", query: [:],
                                                  headers: ["authorization": "Bearer secret"]))

        #expect(response.status == .ok)
        #expect(response.contentType == "application/octet-stream")
        #expect(response.body == bytes)
    }

    @Test func rejectsUnauthorizedSnapshotRequest() async {
        let handler = SnapshotHTTPHandler(
            repository: SnapshotRepository(),
            authenticator: HTTPAuthenticator(apiToken: "secret"),
            scheduler: RefreshScheduler { _ in })

        let response = await handler.handle(.init(method: "GET", path: "/v1/snapshot", query: [:], headers: [:]))
        #expect(response.status == .unauthorized)
        #expect(String(decoding: response.body, as: UTF8.self).contains("unauthorized"))
    }

    @Test func refreshSchedulesScopeAndReturnsAccepted() async {
        final class Box: @unchecked Sendable {
            var scopes: [UInt8] = []
        }
        let box = Box()
        let handler = SnapshotHTTPHandler(
            repository: SnapshotRepository(),
            authenticator: HTTPAuthenticator(apiToken: "secret"),
            scheduler: RefreshScheduler { scope in box.scopes.append(scope) })

        let response = await handler.handle(.init(method: "POST", path: "/v1/refresh", query: ["scope": "4"],
                                                  headers: ["authorization": "Bearer secret"]))
        #expect(response.status == .accepted)
        #expect(box.scopes == [4])
    }

    @Test func parserPreservesIncomingMethodCase() {
        let request = LocalHTTPServer.parseForTesting(requestData("get /v1/health HTTP/1.1"))

        #expect(request?.method == "get")
    }

    @Test func parserSplitsPathAndQueryWithLastDuplicateValueWinning() {
        let request = LocalHTTPServer.parseForTesting(
            requestData("POST /v1/refresh?scope=4&scope=5&name=a%20b HTTP/1.1"))

        #expect(request?.path == "/v1/refresh")
        #expect(request?.query["scope"] == "5")
        #expect(request?.query["name"] == "a b")
    }

    @Test func parserRejectsHeaderTerminatorAfterReadCap() {
        let prefix = "GET /v1/health HTTP/1.1\r\nHost: localhost\r\nX-Fill: "
        let filler = String(repeating: "a", count: 16_384 - prefix.utf8.count + 1)
        let data = Data((prefix + filler + "\r\n\r\n").utf8)

        #expect(LocalHTTPServer.parseForTesting(data) == nil)
    }

    @Test func parserLowercasesHeaderNames() {
        let request = LocalHTTPServer.parseForTesting(
            requestData("GET /v1/snapshot HTTP/1.1", headers: ["Authorization": "Bearer secret"]))

        #expect(request?.headers["authorization"] == "Bearer secret")
        #expect(request?.headers["Authorization"] == nil)
    }

    private func requestData(_ firstLine: String, headers: [String: String] = ["Host": "localhost"]) -> Data {
        var raw = firstLine + "\r\n"
        for (name, value) in headers {
            raw += "\(name): \(value)\r\n"
        }
        raw += "\r\n"
        return Data(raw.utf8)
    }
}
