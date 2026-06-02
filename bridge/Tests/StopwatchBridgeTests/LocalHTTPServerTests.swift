import Foundation
import Testing
@testable import StopwatchBridge
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif

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

    @Test func refreshSchedulesScopeAndReturnsAccepted() async throws {
        let box = ScopeBox()
        let handler = SnapshotHTTPHandler(
            repository: SnapshotRepository(),
            authenticator: HTTPAuthenticator(apiToken: "secret"),
            scheduler: RefreshScheduler { scope in await box.complete(scope) })

        let response = await handler.handle(.init(method: "POST", path: "/v1/refresh", query: ["scope": "4"],
                                                  headers: ["authorization": "Bearer secret"]))
        #expect(response.status == .accepted)
        let completed = try await waitForScopes(in: box, matching: [4])
        #expect(completed == [4])
    }

    @Test func schedulerCancelsSupersededRefreshAndCompletesLatest() async throws {
        let box = ScopeBox()
        let scheduler = RefreshScheduler { scope in
            await box.start(scope)
            do {
                try await Task.sleep(nanoseconds: scope == 1 ? 500_000_000 : 10_000_000)
                await box.complete(scope)
            } catch {
                await box.cancel(scope)
            }
        }

        await scheduler.schedule(scope: 1)
        _ = try await waitForStartedScopes(in: box, matching: [1])
        await scheduler.schedule(scope: 2)

        let completed = try await waitForScopes(in: box, matching: [2])
        let cancelled = await box.cancelledScopes()
        #expect(completed == [2])
        #expect(cancelled == [1])
    }

    @Test func loopbackServerStopsAndRestartsOnSamePort() async throws {
        let port = try freeLoopbackPort()
        let server = LocalHTTPServer(host: "127.0.0.1", port: port) { request in
            #expect(request.path == "/v1/health")
            return .json(.ok, #"{"status":"ok"}"#)
        }

        server.start()
        let first = try await waitForHTTPHealth(port: port)
        #expect(first.contains("HTTP/1.1 200 OK"))
        #expect(first.contains(#"{"status":"ok"}"#))

        server.stop()
        try await waitForPortClosed(port: port)

        server.start()
        let second = try await waitForHTTPHealth(port: port)
        #expect(second.contains("HTTP/1.1 200 OK"))
        #expect(second.contains(#"{"status":"ok"}"#))
        server.stop()
        try await waitForPortClosed(port: port)
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

    private func waitForStartedScopes(in box: ScopeBox, matching expected: [UInt8]) async throws -> [UInt8] {
        try await waitUntil {
            let scopes = await box.startedScopes()
            return scopes == expected ? scopes : nil
        }
    }

    private func waitForScopes(in box: ScopeBox, matching expected: [UInt8]) async throws -> [UInt8] {
        try await waitUntil {
            let scopes = await box.completedScopes()
            return scopes == expected ? scopes : nil
        }
    }

    private func waitUntil<T>(_ condition: () async -> T?) async throws -> T {
        let deadline = Date().addingTimeInterval(1.0)
        while Date() < deadline {
            if let value = await condition() {
                return value
            }
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        throw TestTimeout()
    }

    private func waitForHTTPHealth(port: UInt16) async throws -> String {
        try await waitUntil {
            try? fetchHealth(port: port)
        }
    }

    private func waitForPortClosed(port: UInt16) async throws {
        let closed = try await waitUntil { () -> Bool? in
            if let fd = connectToLoopback(port: port) {
                closeSocket(fd)
                return nil
            }
            return true
        }
        #expect(closed)
    }
}

private actor ScopeBox {
    private var started: [UInt8] = []
    private var completed: [UInt8] = []
    private var cancelled: [UInt8] = []

    func start(_ scope: UInt8) {
        started.append(scope)
    }

    func complete(_ scope: UInt8) {
        completed.append(scope)
    }

    func cancel(_ scope: UInt8) {
        cancelled.append(scope)
    }

    func startedScopes() -> [UInt8] {
        started
    }

    func completedScopes() -> [UInt8] {
        completed
    }

    func cancelledScopes() -> [UInt8] {
        cancelled
    }
}

private struct TestTimeout: Error {}

private func freeLoopbackPort() throws -> UInt16 {
    let fd = try makeSocket()
    defer { closeSocket(fd) }

    var address = sockaddr_in()
    #if canImport(Darwin)
    address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
    #endif
    address.sin_family = sa_family_t(AF_INET)
    address.sin_port = 0
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr)

    let bound = withUnsafePointer(to: &address) { pointer in
        pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) { socketAddress in
            bind(fd, socketAddress, socklen_t(MemoryLayout<sockaddr_in>.size))
        }
    }
    guard bound == 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }

    var assigned = sockaddr_in()
    var length = socklen_t(MemoryLayout<sockaddr_in>.size)
    let result = withUnsafeMutablePointer(to: &assigned) { pointer in
        pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) { socketAddress in
            getsockname(fd, socketAddress, &length)
        }
    }
    guard result == 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
    return UInt16(bigEndian: assigned.sin_port)
}

private func fetchHealth(port: UInt16) throws -> String {
    guard let fd = connectToLoopback(port: port) else {
        throw POSIXError(.ECONNREFUSED)
    }
    defer { closeSocket(fd) }

    let request = Data("GET /v1/health HTTP/1.1\r\nHost: localhost\r\n\r\n".utf8)
    try sendAll(request, fd: fd)

    var data = Data()
    var buffer = [UInt8](repeating: 0, count: 4096)
    let bufferSize = buffer.count
    while true {
        let count = buffer.withUnsafeMutableBytes { rawBuffer in
            recv(fd, rawBuffer.baseAddress, bufferSize, 0)
        }
        if count > 0 {
            data.append(buffer, count: count)
        } else if count == 0 {
            break
        } else if errno == EINTR {
            continue
        } else {
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
        }
    }
    return String(decoding: data, as: UTF8.self)
}

private func sendAll(_ data: Data, fd: Int32) throws {
    try data.withUnsafeBytes { rawBuffer in
        guard let base = rawBuffer.baseAddress else { return }
        var sent = 0
        while sent < data.count {
            let count = send(fd, base.advanced(by: sent), data.count - sent, 0)
            if count > 0 {
                sent += count
            } else if count == -1, errno == EINTR {
                continue
            } else {
                throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
            }
        }
    }
}

private func connectToLoopback(port: UInt16) -> Int32? {
    guard let fd = try? makeSocket() else { return nil }

    var address = sockaddr_in()
    #if canImport(Darwin)
    address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
    #endif
    address.sin_family = sa_family_t(AF_INET)
    address.sin_port = port.bigEndian
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr)

    let connected = withUnsafePointer(to: &address) { pointer in
        pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) { socketAddress in
            connect(fd, socketAddress, socklen_t(MemoryLayout<sockaddr_in>.size))
        }
    }
    guard connected == 0 else {
        closeSocket(fd)
        return nil
    }
    return fd
}

private func makeSocket() throws -> Int32 {
    #if canImport(Darwin)
    let streamType = SOCK_STREAM
    #else
    let streamType = Int32(SOCK_STREAM.rawValue)
    #endif
    let fd = socket(AF_INET, streamType, 0)
    guard fd >= 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
    return fd
}

private func closeSocket(_ fd: Int32) {
    #if canImport(Darwin)
    Darwin.close(fd)
    #else
    Glibc.close(fd)
    #endif
}
