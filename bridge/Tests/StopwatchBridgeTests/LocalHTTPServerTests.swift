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
}
