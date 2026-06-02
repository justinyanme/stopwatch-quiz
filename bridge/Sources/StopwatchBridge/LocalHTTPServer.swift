import Foundation
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif

private let requestReadTimeoutMilliseconds: Int32 = 5000
private let acceptPollTimeoutMilliseconds: Int32 = 100
private let requestHeaderLimitBytes = 16 * 1024

public struct HTTPRequest: Sendable {
    public var method: String
    public var path: String
    public var query: [String: String]
    public var headers: [String: String]

    public init(method: String, path: String, query: [String: String], headers: [String: String]) {
        self.method = method
        self.path = path
        self.query = query
        self.headers = headers
    }
}

public enum HTTPStatus: Int, Sendable {
    case ok = 200
    case accepted = 202
    case badRequest = 400
    case unauthorized = 401
    case notFound = 404
    case methodNotAllowed = 405
    case internalServerError = 500

    var reason: String {
        switch self {
        case .ok: "OK"
        case .accepted: "Accepted"
        case .badRequest: "Bad Request"
        case .unauthorized: "Unauthorized"
        case .notFound: "Not Found"
        case .methodNotAllowed: "Method Not Allowed"
        case .internalServerError: "Internal Server Error"
        }
    }
}

public struct HTTPResponse: Sendable {
    public var status: HTTPStatus
    public var contentType: String
    public var body: Data

    public init(status: HTTPStatus, contentType: String, body: Data) {
        self.status = status
        self.contentType = contentType
        self.body = body
    }

    public static func json(_ status: HTTPStatus, _ text: String) -> HTTPResponse {
        HTTPResponse(status: status, contentType: "application/json; charset=utf-8", body: Data(text.utf8))
    }

    fileprivate var serialized: Data {
        var headers = "HTTP/1.1 \(status.rawValue) \(status.reason)\r\n"
        headers += "Content-Type: \(contentType)\r\n"
        headers += "Content-Length: \(body.count)\r\n"
        headers += "Connection: close\r\n"
        headers += "\r\n"

        var data = Data(headers.utf8)
        data.append(body)
        return data
    }
}

public struct SnapshotHTTPHandler: Sendable {
    private let repository: SnapshotRepository
    private let authenticator: HTTPAuthenticator
    private let scheduler: RefreshScheduler

    public init(repository: SnapshotRepository, authenticator: HTTPAuthenticator, scheduler: RefreshScheduler) {
        self.repository = repository
        self.authenticator = authenticator
        self.scheduler = scheduler
    }

    public func handle(_ request: HTTPRequest) async -> HTTPResponse {
        let route: HTTPRoute
        do {
            route = try HTTPRoute.parse(method: request.method, path: request.path, query: request.query)
        } catch HTTPRouteError.methodNotAllowed {
            return .json(.methodNotAllowed, #"{"error":"method not allowed"}"#)
        } catch HTTPRouteError.badRequest {
            return .json(.badRequest, #"{"error":"bad request"}"#)
        } catch {
            return .json(.notFound, #"{"error":"not found"}"#)
        }

        if route != .health, !authenticator.isAuthorized(headers: request.headers) {
            return .json(.unauthorized, #"{"error":"unauthorized"}"#)
        }

        switch route {
        case .health:
            return .json(.ok, #"{"status":"ok"}"#)
        case let .read(kind):
            return HTTPResponse(status: .ok, contentType: "application/octet-stream", body: await repository.bytes(for: kind))
        case let .refresh(scope):
            await scheduler.schedule(scope: scope)
            return .json(.accepted, #"{"status":"scheduled"}"#)
        }
    }
}

public final class LocalHTTPServer: @unchecked Sendable {
    public typealias Handler = @Sendable (HTTPRequest) async -> HTTPResponse

    private let host: String
    private let port: UInt16
    private let handler: Handler
    private let lock = NSLock()
    private var task: Task<Void, Never>?
    private var runID: UInt64 = 0

    public init(host: String, port: UInt16, handler: @escaping Handler) {
        self.host = host
        self.port = port
        self.handler = handler
    }

    public func start() {
        lock.lock()
        defer { lock.unlock() }
        guard task == nil else { return }

        runID &+= 1
        let currentRunID = runID
        task = Task { [weak self, host, port, handler] in
            defer { self?.clearTaskIfCurrentRun(currentRunID) }
            do {
                try await LocalHTTPServer.runLoop(host: host, port: port, handler: handler)
            } catch {
                if !Task.isCancelled {
                    FileHandle.standardError.write(Data("http server failed: \(error)\n".utf8))
                }
            }
        }
    }

    public func stop() {
        lock.lock()
        defer { lock.unlock() }
        task?.cancel()
    }

    static func parseForTesting(_ data: Data) -> HTTPRequest? {
        guard case let .success(parsedRequest) = parseRequestData(data) else {
            return nil
        }
        return parsedRequest.request
    }

    private func clearTaskIfCurrentRun(_ currentRunID: UInt64) {
        lock.lock()
        defer { lock.unlock() }
        guard runID == currentRunID else { return }
        task = nil
    }

    private static func runLoop(host: String, port: UInt16, handler: @escaping Handler) async throws {
        ignoreSIGPIPE()

        #if canImport(Darwin)
        let streamType = SOCK_STREAM
        #else
        let streamType = Int32(SOCK_STREAM.rawValue)
        #endif

        let serverFD = socket(AF_INET, streamType, 0)
        guard serverFD >= 0 else {
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
        }
        defer { closeSocket(serverFD) }

        var reuse: Int32 = 1
        setsockopt(
            serverFD,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            socklen_t(MemoryLayout<Int32>.size))

        var address = sockaddr_in()
        #if canImport(Darwin)
        address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        #endif
        address.sin_family = sa_family_t(AF_INET)
        address.sin_port = port.bigEndian
        guard isIPv4Loopback(host), inet_pton(AF_INET, host, &address.sin_addr) == 1 else {
            throw POSIXError(.EADDRNOTAVAIL)
        }

        let bound = withUnsafePointer(to: &address) { pointer in
            pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) { socketAddress in
                bind(serverFD, socketAddress, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard bound == 0 else {
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
        }

        guard listen(serverFD, 16) == 0 else {
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
        }

        while !Task.isCancelled {
            guard waitForReadable(serverFD, timeoutMilliseconds: acceptPollTimeoutMilliseconds) else {
                continue
            }
            guard !Task.isCancelled else { break }

            var clientAddress = sockaddr()
            var clientLength = socklen_t(MemoryLayout<sockaddr>.size)
            let clientFD = accept(serverFD, &clientAddress, &clientLength)
            guard clientFD >= 0 else { continue }
            Task {
                defer { closeSocket(clientFD) }
                await handleClient(clientFD, handler: handler)
            }
        }
    }
}

private struct ParsedHTTPRequest {
    let method: String
    let path: String
    let query: [String: String]
    let headers: [String: String]

    var request: HTTPRequest {
        HTTPRequest(method: method, path: path, query: query, headers: headers)
    }

    static func parse(_ data: Data) -> Result<ParsedHTTPRequest, HTTPRequestParseError> {
        guard let raw = String(data: data, encoding: .utf8),
              let firstLine = raw.components(separatedBy: "\r\n").first
        else {
            return .failure(.invalidRequest)
        }

        let parts = firstLine.split(separator: " ")
        guard parts.count >= 3 else { return .failure(.invalidRequest) }

        let method = String(parts[0])
        let target = String(parts[1])
        guard target.hasPrefix("/") else { return .failure(.invalidRequest) }

        switch parseHeaders(raw) {
        case let .success(headers):
            let components = URLComponents(string: "http://localhost\(target)")
            let path = components?.path ?? target
            var query: [String: String] = [:]
            for item in components?.queryItems ?? [] {
                if let value = item.value {
                    query[item.name] = value
                }
            }

            return .success(ParsedHTTPRequest(method: method, path: path, query: query, headers: headers))
        case let .failure(error):
            return .failure(error)
        }
    }

    private static func parseHeaders(_ raw: String) -> Result<[String: String], HTTPRequestParseError> {
        let lines = raw.components(separatedBy: "\r\n")
        var headers: [String: String] = [:]

        for line in lines.dropFirst() {
            if line.isEmpty { break }
            guard let separator = line.firstIndex(of: ":") else {
                return .failure(.invalidRequest)
            }
            let name = String(line[..<separator]).trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
            let value = String(line[line.index(after: separator)...]).trimmingCharacters(in: .whitespacesAndNewlines)
            guard !name.isEmpty else { return .failure(.invalidRequest) }
            headers[name] = value
        }

        return .success(headers)
    }
}

private enum HTTPRequestParseError: Error, Equatable {
    case invalidRequest
}

private func parseRequestData(_ data: Data) -> Result<ParsedHTTPRequest, HTTPRequestParseError> {
    guard let headerEnd = data.range(of: Data("\r\n\r\n".utf8)),
          headerEnd.upperBound <= requestHeaderLimitBytes
    else {
        return .failure(.invalidRequest)
    }
    return ParsedHTTPRequest.parse(data)
}

private func handleClient(
    _ clientFD: Int32,
    handler: @Sendable (HTTPRequest) async -> HTTPResponse) async
{
    let request: HTTPRequest
    switch readRequest(clientFD) {
    case let .success(parsedRequest):
        request = parsedRequest.request
    case .failure:
        sendResponse(
            HTTPResponse.json(.badRequest, #"{"error":"invalid request"}"#),
            to: clientFD)
        return
    }

    let response = await handler(request)
    sendResponse(response, to: clientFD)
}

private func readRequest(_ fd: Int32) -> Result<ParsedHTTPRequest, HTTPRequestParseError> {
    var data = Data()
    var buffer = [UInt8](repeating: 0, count: 4096)
    let bufferSize = buffer.count
    var sawHeaderEnd = false

    while data.count < requestHeaderLimitBytes {
        guard waitForReadable(fd, timeoutMilliseconds: requestReadTimeoutMilliseconds) else {
            return .failure(.invalidRequest)
        }
        let remainingCapacity = requestHeaderLimitBytes - data.count
        let readSize = min(bufferSize, remainingCapacity)
        let count = buffer.withUnsafeMutableBytes { rawBuffer in
            recv(fd, rawBuffer.baseAddress, readSize, 0)
        }
        guard count > 0 else { break }
        data.append(buffer, count: count)
        if data.range(of: Data("\r\n\r\n".utf8)) != nil {
            sawHeaderEnd = true
            break
        }
    }

    guard sawHeaderEnd else { return .failure(.invalidRequest) }
    return parseRequestData(data)
}

private func sendResponse(_ response: HTTPResponse, to fd: Int32) {
    let data = response.serialized
    data.withUnsafeBytes { rawBuffer in
        guard let base = rawBuffer.baseAddress else { return }
        var sent = 0
        while sent < data.count {
            let count = send(fd, base.advanced(by: sent), data.count - sent, sendNoSignalFlags())
            if count > 0 {
                sent += count
            } else if count == -1, errno == EINTR {
                continue
            } else {
                break
            }
        }
    }
}

private func waitForReadable(_ fd: Int32, timeoutMilliseconds: Int32) -> Bool {
    var pollFD = pollfd(fd: fd, events: Int16(POLLIN), revents: 0)
    while true {
        let result = poll(&pollFD, 1, timeoutMilliseconds)
        if result > 0 {
            return (pollFD.revents & Int16(POLLIN)) != 0
        }
        if result == -1, errno == EINTR {
            continue
        }
        return false
    }
}

private func sendNoSignalFlags() -> Int32 {
    #if canImport(Darwin)
    0
    #else
    Int32(MSG_NOSIGNAL)
    #endif
}

private func ignoreSIGPIPE() {
    #if canImport(Darwin)
    _ = Darwin.signal(SIGPIPE, SIG_IGN)
    #else
    _ = Glibc.signal(SIGPIPE, SIG_IGN)
    #endif
}

private func closeSocket(_ fd: Int32) {
    #if canImport(Darwin)
    Darwin.close(fd)
    #else
    Glibc.close(fd)
    #endif
}

private func isIPv4Loopback(_ host: String) -> Bool {
    let octets = host.split(separator: ".", omittingEmptySubsequences: false)
    guard octets.count == 4 else { return false }
    guard octets[0] == "127" else { return false }
    return octets.allSatisfy { octet in
        guard let value = UInt8(octet) else { return false }
        return String(value) == octet
    }
}
