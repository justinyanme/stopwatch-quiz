# WiFi Mac Mini Helper Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the WiFi/HTTP Mac mini data path while preserving the existing BLE mode and binary watch payloads.

**Architecture:** Add a loopback Swift HTTP server that serves cached binary snapshot frames and accepts refresh scheduling, then add firmware provisioning plus a manual WiFi/BLE transport selector. The first transport milestone can still feed from existing bridge collectors; the collector tasks remove the CodexBar runtime dependency by adapting non-interactive CodexBar source logic into this repo.

**Tech Stack:** Swift 6.2 on macOS 14+, Foundation/POSIX sockets/CoreBluetooth/Security, Swift Testing, PlatformIO Arduino C++17, M5Unified, NimBLE-Arduino, ESP32 WiFi/HTTPClient/Preferences, Unity native firmware tests, Cloudflare Tunnel/Access.

---

## Scope And Execution Notes

This is a large cross-cutting feature. Execute it in task order. Each task includes its own tests and commit. Use a clean worktree or an isolated worktree before implementation; this repository currently may contain unrelated local bridge changes.

The plan intentionally creates the transport path before the provider-source rewrite:

1. Bridge HTTP/cache/auth path.
2. Firmware provisioning and WiFi transport path.
3. Bridge source rewrite away from CodexBar runtime.

Do not change the binary wire formats unless a task explicitly says to. Existing shared hex fixture tests are the compatibility guard.

## File Structure

Bridge files:

- Modify `bridge/Sources/StopwatchBridge/Config.swift`
  - Adds HTTP bind/port/token settings with backward-compatible decode.
- Create `bridge/Sources/StopwatchBridge/SnapshotRepository.swift`
  - Owns current bytes for `snapshot`, `cost`, `balances`, and `balance-usage`.
- Create `bridge/Sources/StopwatchBridge/HTTPAuth.swift`
  - Validates origin `Authorization: Bearer ...` token.
- Create `bridge/Sources/StopwatchBridge/LocalHTTPServer.swift`
  - Minimal loopback HTTP server using POSIX sockets, adapted from CodexBar's local server shape but local to this repo.
- Create `bridge/Sources/StopwatchBridge/HTTPRoutes.swift`
  - Parses routes and maps them to repository reads or refresh scheduling.
- Create `bridge/Sources/StopwatchBridge/RefreshScheduler.swift`
  - Deduplicates scope refresh requests and calls bridge refresh operations asynchronously.
- Modify `bridge/Sources/StopwatchBridge/BridgeService.swift`
  - Starts HTTP server and uses repository updates alongside GATT updates.
- Modify `bridge/Sources/StopwatchBridge/Bridge.swift`
  - Adds `serve-config` or prints HTTP setup values safely.
- Create bridge tests:
  - `bridge/Tests/StopwatchBridgeTests/HTTPAuthTests.swift`
  - `bridge/Tests/StopwatchBridgeTests/SnapshotRepositoryTests.swift`
  - `bridge/Tests/StopwatchBridgeTests/HTTPRoutesTests.swift`
  - `bridge/Tests/StopwatchBridgeTests/LocalHTTPServerTests.swift`

Firmware files:

- Modify `firmware/src/CarouselSettings.h`
  - Adds `TransportMode` and a settings row for `TRANSPORT`.
- Modify `firmware/src/SettingsCodec.{h,cpp}`
  - Adds v3 settings decode/encode while accepting existing v1/v2 bytes.
- Modify `firmware/src/SnapshotStore.{h,cpp}`
  - Loads either 8-byte v1/v2 settings or new v3 settings.
- Create `firmware/src/DeviceConfig.h`
  - NVS key names and pure config model helpers.
- Create `firmware/src/ProvisioningCommand.{h,cpp}`
  - Pure parser for serial provisioning commands.
- Create `firmware/src/SerialProvisioning.{h,cpp}`
  - Arduino-only serial loop that writes provisioning values to NVS.
- Create `firmware/src/NetworkClient.{h,cpp}`
  - WiFi + HTTP binary fetch implementation.
- Create `firmware/src/TransportClient.{h,cpp}`
  - Chooses `NetworkClient` or `BleClient` based on local settings.
- Modify `firmware/src/main.cpp`
  - Uses `TransportClient`, starts serial provisioning, and maps WiFi/API errors to link status labels.
- Modify `firmware/src/App.h` and provider/overview status label helpers
  - Adds WiFi/API link states.
- Create firmware tests:
  - `firmware/test/test_provisioning_command/test_main.cpp`
  - Extend `firmware/test/test_settings_codec/test_main.cpp`
  - Extend `firmware/test/test_state_machine/test_main.cpp`

Provider collector files:

- Create `bridge/Sources/StopwatchBridge/ProviderCollectors/CodexUsageCollector.swift`
- Create `bridge/Sources/StopwatchBridge/ProviderCollectors/GeminiUsageCollector.swift`
- Create `bridge/Sources/StopwatchBridge/ProviderCollectors/ClaudeUsageCollector.swift`
- Create `bridge/Sources/StopwatchBridge/ProviderCollectors/CostUsage/`
  - Vendored/adapted local cost scanner subset from `/Users/justinyan/Documents/github/CodexBar/Sources/CodexBarCore`.
- Create collector tests:
  - `bridge/Tests/StopwatchBridgeTests/CodexUsageCollectorTests.swift`
  - `bridge/Tests/StopwatchBridgeTests/GeminiUsageCollectorTests.swift`
  - `bridge/Tests/StopwatchBridgeTests/ClaudeUsageCollectorTests.swift`
  - `bridge/Tests/StopwatchBridgeTests/DirectCostCollectorTests.swift`

Documentation:

- Modify `README.md`
- Modify `shared/PROTOCOL.md` only if status labels or route names need documentation; do not alter binary layout.

---

### Task 1: Bridge Config For HTTP Server

**Files:**
- Modify: `bridge/Sources/StopwatchBridge/Config.swift`
- Test: `bridge/Tests/StopwatchBridgeTests/ConfigTests.swift`

- [ ] **Step 1: Write failing config tests**

Add these tests to `ConfigTests.swift`:

```swift
@Test func defaultsIncludeHTTPServerSettings() throws {
    let cfg = Config.makeDefault()
    #expect(cfg.httpBindHost == "127.0.0.1")
    #expect(cfg.httpPort == 8787)
    #expect(cfg.apiToken.count == 64)
    #expect(cfg.apiToken.allSatisfy { $0.isHexDigit })
}

@Test func decodesLegacyConfigWithHTTPDefaults() throws {
    let legacy = Data("""
    {
      "codexbarPort": 54321,
      "serviceUUID": "\(Protocol.serviceUUID.uuidString)",
      "logLevel": "info",
      "spawnCodexbar": true,
      "instanceHash": "abcd"
    }
    """.utf8)

    let decoded = try JSONDecoder().decode(Config.self, from: legacy)
    #expect(decoded.codexbarPort == 54321)
    #expect(decoded.httpBindHost == "127.0.0.1")
    #expect(decoded.httpPort == 8787)
    #expect(decoded.apiToken.count == 64)
}
```

- [ ] **Step 2: Run config tests and verify failure**

Run:

```bash
cd bridge && swift test --filter ConfigTests
```

Expected: fails because `Config` has no `httpBindHost`, `httpPort`, or `apiToken`.

- [ ] **Step 3: Implement backward-compatible config fields**

Replace `Config.swift` with a custom Codable implementation that preserves existing fields:

```swift
import Foundation
import Security

public struct Config: Codable, Equatable, Sendable {
    public var codexbarPort: UInt16
    public var serviceUUID: String
    public var logLevel: String
    public var spawnCodexbar: Bool
    public var instanceHash: String
    public var httpBindHost: String
    public var httpPort: UInt16
    public var apiToken: String

    private enum CodingKeys: String, CodingKey {
        case codexbarPort, serviceUUID, logLevel, spawnCodexbar, instanceHash
        case httpBindHost, httpPort, apiToken
    }

    public init(
        codexbarPort: UInt16,
        serviceUUID: String,
        logLevel: String,
        spawnCodexbar: Bool,
        instanceHash: String,
        httpBindHost: String,
        httpPort: UInt16,
        apiToken: String)
    {
        self.codexbarPort = codexbarPort
        self.serviceUUID = serviceUUID
        self.logLevel = logLevel
        self.spawnCodexbar = spawnCodexbar
        self.instanceHash = instanceHash
        self.httpBindHost = httpBindHost
        self.httpPort = httpPort
        self.apiToken = apiToken
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        self.codexbarPort = try c.decode(UInt16.self, forKey: .codexbarPort)
        self.serviceUUID = try c.decode(String.self, forKey: .serviceUUID)
        self.logLevel = try c.decode(String.self, forKey: .logLevel)
        self.spawnCodexbar = try c.decode(Bool.self, forKey: .spawnCodexbar)
        self.instanceHash = try c.decode(String.self, forKey: .instanceHash)
        self.httpBindHost = try c.decodeIfPresent(String.self, forKey: .httpBindHost) ?? "127.0.0.1"
        self.httpPort = try c.decodeIfPresent(UInt16.self, forKey: .httpPort) ?? 8787
        self.apiToken = try c.decodeIfPresent(String.self, forKey: .apiToken) ?? Self.randomHex(byteCount: 32)
    }

    public static let defaultPath: URL = {
        let support = FileManager.default
            .urls(for: .applicationSupportDirectory, in: .userDomainMask)
            .first!
        return support.appendingPathComponent("stopwatch-bridge/config.json")
    }()

    public static func makeDefault() -> Config {
        let port = UInt16.random(in: 49152...65535)
        return .init(
            codexbarPort: port,
            serviceUUID: Protocol.serviceUUID.uuidString,
            logLevel: "info",
            spawnCodexbar: true,
            instanceHash: randomHex(byteCount: 2),
            httpBindHost: "127.0.0.1",
            httpPort: 8787,
            apiToken: randomHex(byteCount: 32)
        )
    }

    public static func load(from url: URL = defaultPath) throws -> Config? {
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }
        let data = try Data(contentsOf: url)
        return try JSONDecoder().decode(Config.self, from: data)
    }

    public static func save(_ cfg: Config, to url: URL = defaultPath) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(),
                                                withIntermediateDirectories: true)
        let enc = JSONEncoder()
        enc.outputFormatting = [.prettyPrinted, .sortedKeys]
        let prev = umask(0o177)
        defer { umask(prev) }
        try enc.encode(cfg).write(to: url, options: .atomic)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path)
    }

    private static func randomHex(byteCount: Int) -> String {
        var bytes = [UInt8](repeating: 0, count: byteCount)
        let status = SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes)
        if status != errSecSuccess {
            let seed = UUID().uuidString.replacingOccurrences(of: "-", with: "")
            return String(seed.prefix(byteCount * 2)).padding(toLength: byteCount * 2, withPad: "0", startingAt: 0)
        }
        return bytes.map { String(format: "%02x", $0) }.joined()
    }
}
```

- [ ] **Step 4: Run config tests**

Run:

```bash
cd bridge && swift test --filter ConfigTests
```

Expected: all `ConfigTests` pass.

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/Config.swift bridge/Tests/StopwatchBridgeTests/ConfigTests.swift
git commit -m "bridge: add http server config"
```

---

### Task 2: Bridge Snapshot Repository

**Files:**
- Create: `bridge/Sources/StopwatchBridge/SnapshotRepository.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/SnapshotRepositoryTests.swift`

- [ ] **Step 1: Write failing repository tests**

Create `SnapshotRepositoryTests.swift`:

```swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct SnapshotRepositoryTests {
    @Test func startsWithWellFormedEmptyFrames() async {
        let repo = SnapshotRepository()
        #expect(await repo.bytes(for: .snapshot).count == Protocol.snapshotSize)
        #expect(await repo.bytes(for: .cost).count >= Protocol.costHeaderSize)
        #expect(await repo.bytes(for: .balances).count >= Protocol.balanceHeaderSize)
        #expect(await repo.bytes(for: .balanceUsage).count >= Protocol.usageHeaderSize)
    }

    @Test func storesAndReturnsUpdatedFrames() async {
        let repo = SnapshotRepository()
        let snapshot = SnapshotEncoder.encodeGATTSnapshot(.threeProvidersFixture)
        await repo.update(.snapshot, bytes: snapshot)
        #expect(await repo.bytes(for: .snapshot) == snapshot)
    }

    @Test func rejectsInvalidSnapshotLength() async {
        let repo = SnapshotRepository()
        let before = await repo.bytes(for: .snapshot)
        await repo.update(.snapshot, bytes: Data([1, 2, 3]))
        #expect(await repo.bytes(for: .snapshot) == before)
    }
}
```

- [ ] **Step 2: Run repository tests and verify failure**

Run:

```bash
cd bridge && swift test --filter SnapshotRepositoryTests
```

Expected: fails because `SnapshotRepository` does not exist.

- [ ] **Step 3: Implement repository**

Create `SnapshotRepository.swift`:

```swift
import Foundation

public enum SnapshotKind: String, Sendable, CaseIterable {
    case snapshot
    case cost
    case balances
    case balanceUsage
}

public actor SnapshotRepository {
    private var snapshot: Data
    private var cost: Data
    private var balances: Data
    private var balanceUsage: Data

    public init(
        snapshot: Data = SnapshotEncoder.staleEmpty(),
        cost: Data = CostEncoder.staleEmpty(),
        balances: Data = BalanceEncoder.staleEmpty(),
        balanceUsage: Data = UsageEncoder.staleEmpty())
    {
        self.snapshot = snapshot
        self.cost = cost
        self.balances = balances
        self.balanceUsage = balanceUsage
    }

    public func bytes(for kind: SnapshotKind) -> Data {
        switch kind {
        case .snapshot: return snapshot
        case .cost: return cost
        case .balances: return balances
        case .balanceUsage: return balanceUsage
        }
    }

    public func update(_ kind: SnapshotKind, bytes: Data) {
        guard Self.isValid(bytes, for: kind) else { return }
        switch kind {
        case .snapshot: snapshot = bytes
        case .cost: cost = bytes
        case .balances: balances = bytes
        case .balanceUsage: balanceUsage = bytes
        }
    }

    private static func isValid(_ bytes: Data, for kind: SnapshotKind) -> Bool {
        switch kind {
        case .snapshot:
            return bytes.count == Protocol.snapshotSize
        case .cost:
            return bytes.count >= Protocol.costHeaderSize
        case .balances:
            return bytes.count >= Protocol.balanceHeaderSize
        case .balanceUsage:
            return bytes.count >= Protocol.usageHeaderSize
        }
    }
}
```

- [ ] **Step 4: Run repository tests**

Run:

```bash
cd bridge && swift test --filter SnapshotRepositoryTests
```

Expected: all repository tests pass.

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/SnapshotRepository.swift bridge/Tests/StopwatchBridgeTests/SnapshotRepositoryTests.swift
git commit -m "bridge: add snapshot repository"
```

---

### Task 3: Bridge HTTP Auth And Route Parser

**Files:**
- Create: `bridge/Sources/StopwatchBridge/HTTPAuth.swift`
- Create: `bridge/Sources/StopwatchBridge/HTTPRoutes.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/HTTPAuthTests.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/HTTPRoutesTests.swift`

- [ ] **Step 1: Write auth tests**

Create `HTTPAuthTests.swift`:

```swift
import Testing
@testable import StopwatchBridge

@Suite struct HTTPAuthTests {
    @Test func acceptsExactBearerToken() {
        let auth = HTTPAuthenticator(apiToken: "secret")
        #expect(auth.isAuthorized(headers: ["authorization": "Bearer secret"]))
    }

    @Test func rejectsMissingWrongAndMalformedToken() {
        let auth = HTTPAuthenticator(apiToken: "secret")
        #expect(!auth.isAuthorized(headers: [:]))
        #expect(!auth.isAuthorized(headers: ["authorization": "secret"]))
        #expect(!auth.isAuthorized(headers: ["authorization": "Bearer wrong"]))
        #expect(!auth.isAuthorized(headers: ["authorization": "bearer secret"]))
    }

    @Test func disabledTokenRejectsAllRequests() {
        let auth = HTTPAuthenticator(apiToken: "")
        #expect(!auth.isAuthorized(headers: ["authorization": "Bearer "]))
    }
}
```

- [ ] **Step 2: Write route tests**

Create `HTTPRoutesTests.swift`:

```swift
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
```

- [ ] **Step 3: Run auth and route tests and verify failure**

Run:

```bash
cd bridge && swift test --filter HTTPAuthTests
cd bridge && swift test --filter HTTPRoutesTests
```

Expected: both fail because the new types do not exist.

- [ ] **Step 4: Implement auth**

Create `HTTPAuth.swift`:

```swift
public struct HTTPAuthenticator: Sendable {
    private let apiToken: String

    public init(apiToken: String) {
        self.apiToken = apiToken
    }

    public func isAuthorized(headers: [String: String]) -> Bool {
        guard !apiToken.isEmpty else { return false }
        guard let raw = headers["authorization"] else { return false }
        return raw == "Bearer \(apiToken)"
    }
}
```

- [ ] **Step 5: Implement routes**

Create `HTTPRoutes.swift`:

```swift
public enum HTTPRoute: Equatable, Sendable {
    case health
    case read(SnapshotKind)
    case refresh(UInt8)

    public static func parse(method: String, path: String, query: [String: String]) throws -> HTTPRoute {
        let normalizedMethod = method.uppercased()
        switch path {
        case "/v1/health":
            guard normalizedMethod == "GET" else { throw HTTPRouteError.methodNotAllowed }
            return .health
        case "/v1/snapshot":
            guard normalizedMethod == "GET" else { throw HTTPRouteError.methodNotAllowed }
            return .read(.snapshot)
        case "/v1/cost":
            guard normalizedMethod == "GET" else { throw HTTPRouteError.methodNotAllowed }
            return .read(.cost)
        case "/v1/balances":
            guard normalizedMethod == "GET" else { throw HTTPRouteError.methodNotAllowed }
            return .read(.balances)
        case "/v1/balance-usage":
            guard normalizedMethod == "GET" else { throw HTTPRouteError.methodNotAllowed }
            return .read(.balanceUsage)
        case "/v1/refresh":
            guard normalizedMethod == "POST" else { throw HTTPRouteError.methodNotAllowed }
            guard let raw = query["scope"],
                  let intValue = Int(raw),
                  intValue >= 0,
                  intValue <= 255
            else {
                throw HTTPRouteError.badRequest
            }
            return .refresh(UInt8(intValue))
        default:
            throw HTTPRouteError.notFound
        }
    }
}

public enum HTTPRouteError: Error, Equatable, Sendable {
    case methodNotAllowed
    case notFound
    case badRequest
}
```

- [ ] **Step 6: Run auth and route tests**

Run:

```bash
cd bridge && swift test --filter HTTPAuthTests
cd bridge && swift test --filter HTTPRoutesTests
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add bridge/Sources/StopwatchBridge/HTTPAuth.swift bridge/Sources/StopwatchBridge/HTTPRoutes.swift bridge/Tests/StopwatchBridgeTests/HTTPAuthTests.swift bridge/Tests/StopwatchBridgeTests/HTTPRoutesTests.swift
git commit -m "bridge: add http auth and routes"
```

---

### Task 4: Bridge HTTP Handler And Local Server

**Files:**
- Create: `bridge/Sources/StopwatchBridge/LocalHTTPServer.swift`
- Create: `bridge/Sources/StopwatchBridge/RefreshScheduler.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/LocalHTTPServerTests.swift`

- [ ] **Step 1: Write handler tests without opening a socket**

Create `LocalHTTPServerTests.swift`:

```swift
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
```

- [ ] **Step 2: Run handler tests and verify failure**

Run:

```bash
cd bridge && swift test --filter HTTPHandlerTests
```

Expected: fails because handler/server types do not exist.

- [ ] **Step 3: Implement HTTP request/response and scheduler**

Create `RefreshScheduler.swift`:

```swift
public actor RefreshScheduler {
    private let onRefresh: @Sendable (UInt8) -> Void

    public init(onRefresh: @escaping @Sendable (UInt8) -> Void) {
        self.onRefresh = onRefresh
    }

    public nonisolated func schedule(scope: UInt8) {
        onRefresh(scope)
    }
}
```

Create the top of `LocalHTTPServer.swift`:

```swift
import Foundation

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
        case .ok: return "OK"
        case .accepted: return "Accepted"
        case .badRequest: return "Bad Request"
        case .unauthorized: return "Unauthorized"
        case .notFound: return "Not Found"
        case .methodNotAllowed: return "Method Not Allowed"
        case .internalServerError: return "Internal Server Error"
        }
    }
}

public struct HTTPResponse: Sendable {
    public var status: HTTPStatus
    public var contentType: String
    public var body: Data

    public static func json(_ status: HTTPStatus, _ text: String) -> HTTPResponse {
        HTTPResponse(status: status, contentType: "application/json; charset=utf-8", body: Data(text.utf8))
    }
}
```

- [ ] **Step 4: Implement snapshot handler**

Append to `LocalHTTPServer.swift`:

```swift
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
            return HTTPResponse(
                status: .ok,
                contentType: "application/octet-stream",
                body: await repository.bytes(for: kind))
        case let .refresh(scope):
            scheduler.schedule(scope: scope)
            return .json(.accepted, #"{"status":"scheduled"}"#)
        }
    }
}
```

- [ ] **Step 5: Add socket server by adapting the local HTTP server source**

Append a POSIX loopback server to `LocalHTTPServer.swift`. Use this public API so `BridgeService` can own it:

```swift
public final class LocalHTTPServer: @unchecked Sendable {
    public typealias Handler = @Sendable (HTTPRequest) async -> HTTPResponse

    private let host: String
    private let port: UInt16
    private let handler: Handler
    private var task: Task<Void, Never>?

    public init(host: String, port: UInt16, handler: @escaping Handler) {
        self.host = host
        self.port = port
        self.handler = handler
    }

    public func start() {
        guard task == nil else { return }
        task = Task { [host, port, handler] in
            do {
                try await LocalHTTPServer.runLoop(host: host, port: port, handler: handler)
            } catch {
                FileHandle.standardError.write(Data("http server failed: \(error)\n".utf8))
            }
        }
    }

    public func stop() {
        task?.cancel()
        task = nil
    }
}
```

Use `/Users/justinyan/Documents/github/CodexBar/Sources/CodexBarCLI/CLILocalHTTPServer.swift` as the source to adapt. Copy the socket/read/write helpers into this repo and make these substitutions:

| CodexBar symbol | Stopwatch symbol |
|---|---|
| `CLILocalHTTPRequest` | `HTTPRequest` |
| `CLILocalHTTPResponse` | `HTTPResponse` |
| `CLIHTTPStatus` | `HTTPStatus` |
| `CLILocalHTTPServer` | `LocalHTTPServer` |
| `queryItems` | `query` |

Keep these parsing rules in the adapted code:

- accept only IPv4 loopback bind from `host`;
- read until `\r\n\r\n` or 16 KB;
- lower-case header names;
- parse query string with `URLComponents(string: "http://localhost\(target)")`;
- spawn one `Task` per accepted connection;
- close every accepted file descriptor.

- [ ] **Step 6: Run handler tests**

Run:

```bash
cd bridge && swift test --filter HTTPHandlerTests
```

Expected: handler tests pass and `LocalHTTPServer.swift` compiles without importing `CodexBarCore` or `CodexBarCLI`.

- [ ] **Step 7: Commit**

```bash
git add bridge/Sources/StopwatchBridge/LocalHTTPServer.swift bridge/Sources/StopwatchBridge/RefreshScheduler.swift bridge/Tests/StopwatchBridgeTests/LocalHTTPServerTests.swift
git commit -m "bridge: serve binary snapshots over http"
```

---

### Task 5: BridgeService HTTP Wiring

**Files:**
- Modify: `bridge/Sources/StopwatchBridge/BridgeService.swift`
- Modify: `bridge/Sources/StopwatchBridge/Bridge.swift`
- Test: `bridge/Tests/StopwatchBridgeTests/BridgeServiceTests.swift`

- [ ] **Step 1: Write service-level test for cost cascade helper**

If `BridgeServiceTests.swift` already exists locally, keep its tests and add:

```swift
@Test func refreshScopeNamesStayAlignedWithProtocol() {
    #expect(Protocol.triggerScopeCost == 0x04)
    #expect(Protocol.triggerScopeBalances == 0x05)
    #expect(Protocol.triggerScopeUsage == 0x06)
}
```

- [ ] **Step 2: Add repository and server properties**

Modify `BridgeService` properties:

```swift
private let repository: SnapshotRepository
private let httpServer: LocalHTTPServer
```

In `init(config:)`, after `self.peripheral = await GATTPeripheral()`, initialize repository/server:

```swift
self.repository = SnapshotRepository()
let repo = self.repository
let scheduler = RefreshScheduler { [weakSelf = WeakBridgeServiceBox()] scope in
    Task { await weakSelf.service?.handleRefresh(scope: scope) }
}
self.httpServer = LocalHTTPServer(
    host: config.httpBindHost,
    port: config.httpPort,
    handler: SnapshotHTTPHandler(
        repository: repo,
        authenticator: HTTPAuthenticator(apiToken: config.apiToken),
        scheduler: scheduler
    ).handle)
```

Use this small helper to avoid capturing an actor before all stored properties are initialized:

```swift
private final class WeakBridgeServiceBox: @unchecked Sendable {
    weak var service: BridgeService?
}
```

After `self.httpServer` assignment, set `weakBox.service = self`. If Swift initialization rules make that shape awkward, create the `RefreshScheduler` with a closure assigned after init by making `RefreshScheduler` store an optional callback actor method.

- [ ] **Step 3: Start HTTP server in `run()`**

In `run()`, after setting the GATT delegate, start the HTTP server:

```swift
httpServer.start()
FileHandle.standardOutput.write(Data("http listening on http://\(config.httpBindHost):\(config.httpPort)\n".utf8))
```

- [ ] **Step 4: Update repository whenever GATT snapshots update**

In `handleRefresh(scope:)`, after every `peripheral.update...` call, also update the repository:

```swift
await repository.update(.snapshot, bytes: bytes)
```

For failure paths:

```swift
let failed = snapshotCache.recordFailure()
await peripheral.updateSnapshot(failed)
await repository.update(.snapshot, bytes: failed)
```

Repeat for `.cost`, `.balances`, and `.balanceUsage`.

- [ ] **Step 5: Add safe config output command**

In `Bridge.swift`, add command:

```swift
case "serve-config": printServeConfig()
```

Add usage line:

```text
serve-config               Print HTTP server URL and redacted token metadata
```

Add method:

```swift
static func printServeConfig() {
    do {
        let cfg = try Config.load() ?? Config.makeDefault()
        print("url=http://\(cfg.httpBindHost):\(cfg.httpPort)")
        print("apiToken=set length=\(cfg.apiToken.count)")
    } catch {
        FileHandle.standardError.write(Data("config error: \(error)\n".utf8))
        exit(1)
    }
}
```

- [ ] **Step 6: Run bridge tests**

Run:

```bash
cd bridge && swift test
```

Expected: all bridge tests pass.

- [ ] **Step 7: Commit**

```bash
git add bridge/Sources/StopwatchBridge/BridgeService.swift bridge/Sources/StopwatchBridge/Bridge.swift bridge/Tests/StopwatchBridgeTests/BridgeServiceTests.swift
git commit -m "bridge: wire http server into service"
```

---

### Task 6: Firmware Transport Mode Settings

**Files:**
- Modify: `firmware/src/CarouselSettings.h`
- Modify: `firmware/src/SettingsCodec.{h,cpp}`
- Modify: `firmware/src/SnapshotStore.cpp`
- Modify: `firmware/src/Views/CarouselSettings.cpp`
- Test: `firmware/test/test_settings_codec/test_main.cpp`
- Test: `firmware/test/test_state_machine/test_main.cpp`

- [ ] **Step 1: Write failing settings tests**

Append to `test_settings_codec/test_main.cpp`:

```cpp
void test_roundTripsVersion3TransportMode(void) {
    CarouselSettings in = CarouselSettings::defaults();
    in.transportMode = TransportMode::WiFi;

    uint8_t bytes[kSettingsMaxBytesSize];
    size_t len = 0;
    TEST_ASSERT_TRUE(encodeCarouselSettings(in, bytes, sizeof(bytes), len));
    TEST_ASSERT_EQUAL_UINT8(3, bytes[0]);
    TEST_ASSERT_EQUAL_UINT(kSettingsV3BytesSize, len);

    CarouselSettings out;
    TEST_ASSERT_TRUE(decodeCarouselSettings(bytes, len, out));
    TEST_ASSERT_EQUAL((int)TransportMode::WiFi, (int)out.transportMode);
}

void test_decodesVersion2TransportAsBLE(void) {
    uint8_t bytes[kSettingsV2BytesSize] = {
        2, 0x03, (uint8_t)CarouselMotionMode::Iris, 0,
        10, 0,
        20, 0,
    };
    CarouselSettings out;
    TEST_ASSERT_TRUE(decodeCarouselSettings(bytes, sizeof(bytes), out));
    TEST_ASSERT_EQUAL((int)TransportMode::BLE, (int)out.transportMode);
}
```

Register both tests in `main()`.

- [ ] **Step 2: Write failing state-machine test**

Append to `test_state_machine/test_main.cpp`:

```cpp
void test_carouselSettingsTransportRowToggles(void) {
    App app; app.begin();
    CarouselSettings settings = CarouselSettings::defaults();
    app.handleEvent(ButtonEvent::BothLong, settings);

    while (app.carouselSettingRow() != CarouselSettingRow::Transport) {
        app.handleEvent(ButtonEvent::KeyBShort, settings);
    }

    TEST_ASSERT_EQUAL((int)TransportMode::BLE, (int)settings.transportMode);
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort, settings));
    TEST_ASSERT_EQUAL((int)TransportMode::WiFi, (int)settings.transportMode);
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort, settings));
    TEST_ASSERT_EQUAL((int)TransportMode::BLE, (int)settings.transportMode);
}
```

Register it in `main()`.

- [ ] **Step 3: Run native tests and verify failure**

Run:

```bash
cd firmware && pio test -e native -f test_settings_codec
cd firmware && pio test -e native -f test_state_machine
```

Expected: fails because `TransportMode` and v3 settings do not exist.

- [ ] **Step 4: Add transport mode to settings model**

Modify `CarouselSettings.h`:

```cpp
enum class TransportMode : uint8_t { BLE = 0, WiFi = 1 };

enum class CarouselSettingRow : uint8_t {
    Transport = 0,
    Upright = 1,
    Autoplay = 2,
    Interval = 3,
    Motion = 4,
    Resume = 5,
};
```

Add to `CarouselSettings`:

```cpp
TransportMode transportMode = TransportMode::BLE;
static constexpr uint8_t kRowCount = 6;
```

In `validate()`:

```cpp
if (transportMode != TransportMode::BLE && transportMode != TransportMode::WiFi) {
    transportMode = TransportMode::BLE;
}
```

In `cycle()`:

```cpp
case CarouselSettingRow::Transport:
    transportMode = (transportMode == TransportMode::BLE) ? TransportMode::WiFi : TransportMode::BLE;
    break;
```

In `rowLabel()`:

```cpp
case CarouselSettingRow::Transport: return "TRANSPORT";
```

Add:

```cpp
static const char *transportLabel(TransportMode mode) {
    switch (mode) {
        case TransportMode::BLE:  return "BLE";
        case TransportMode::WiFi: return "WIFI";
    }
    return "BLE";
}
```

Update `nextSettingRow()` order to start with `Transport`, then `Upright`, `Autoplay`, `Interval`, `Motion`, `Resume`, and wrap back to `Transport`.

- [ ] **Step 5: Update settings codec for v3**

Modify `SettingsCodec.h`:

```cpp
constexpr size_t kSettingsV2BytesSize = 8;
constexpr size_t kSettingsV3BytesSize = 10;
constexpr size_t kSettingsMaxBytesSize = kSettingsV3BytesSize;
constexpr size_t kSettingsBytesSize = kSettingsMaxBytesSize;
```

Modify `SettingsCodec.cpp`:

```cpp
constexpr uint8_t kVersion3 = 3;
```

In `encodeCarouselSettings`, write v3:

```cpp
outBytes[0] = kVersion3;
outBytes[1] = flags;
outBytes[2] = (uint8_t)copy.motionMode;
outBytes[3] = (uint8_t)copy.transportMode;
writeU16LE(outBytes + 4, copy.intervalSeconds);
writeU16LE(outBytes + 6, copy.resumeSeconds);
outBytes[8] = 0;
outBytes[9] = 0;
outLen = kSettingsV3BytesSize;
```

In `decodeCarouselSettings`, accept v1/v2 length 8 and v3 length 10:

```cpp
if (!bytes) return false;
uint8_t version = bytes[0];
if (version == kVersion1 || version == kVersion2) {
    if (len != kSettingsV2BytesSize) return false;
} else if (version == kVersion3) {
    if (len != kSettingsV3BytesSize) return false;
} else {
    return false;
}
```

Set transport:

```cpp
decoded.transportMode = version == kVersion3 ? (TransportMode)bytes[3] : TransportMode::BLE;
```

- [ ] **Step 6: Update NVS settings load**

Modify `SnapshotStore::loadCarouselSettings` so it does not require one fixed size:

```cpp
size_t sz = prefs.getBytesLength(kCarouselSettingsKey);
if (sz != kSettingsV2BytesSize && sz != kSettingsV3BytesSize) return false;
uint8_t bytes[kSettingsMaxBytesSize];
size_t read = prefs.getBytes(kCarouselSettingsKey, bytes, sz);
return decodeCarouselSettings(bytes, read, out);
```

- [ ] **Step 7: Update settings view**

In `Views/CarouselSettings.cpp`, add `Transport` to `valueText`:

```cpp
case CarouselSettingRow::Transport:
    return CarouselSettings::transportLabel(settings.transportMode);
```

Draw rows with this order and spacing:

```cpp
drawGroup(c, "CONNECTION", 96);
drawRow(c, settings, CarouselSettingRow::Transport, selected, 124);

drawGroup(c, "DISPLAY", 166);
drawRow(c, settings, CarouselSettingRow::Upright, selected, 194);

drawGroup(c, "CAROUSEL", 236);
drawRow(c, settings, CarouselSettingRow::Autoplay, selected, 264);
drawRow(c, settings, CarouselSettingRow::Interval, selected, 310);
drawRow(c, settings, CarouselSettingRow::Motion, selected, 356);
drawRow(c, settings, CarouselSettingRow::Resume, selected, 402);
```

Keep footer text visible by moving it to:

```cpp
c.drawString("A CHANGE  B NEXT", theme::kCenterX, 430);
```

- [ ] **Step 8: Run firmware native tests**

Run:

```bash
cd firmware && pio test -e native -f test_settings_codec
cd firmware && pio test -e native -f test_state_machine
```

Expected: both suites pass.

- [ ] **Step 9: Commit**

```bash
git add firmware/src/CarouselSettings.h firmware/src/SettingsCodec.h firmware/src/SettingsCodec.cpp firmware/src/SnapshotStore.cpp firmware/src/Views/CarouselSettings.cpp firmware/test/test_settings_codec/test_main.cpp firmware/test/test_state_machine/test_main.cpp
git commit -m "firmware: add manual transport setting"
```

---

### Task 7: Firmware Provisioning Parser

**Files:**
- Create: `firmware/src/DeviceConfig.h`
- Create: `firmware/src/ProvisioningCommand.h`
- Create: `firmware/src/ProvisioningCommand.cpp`
- Create: `firmware/test/test_provisioning_command/test_main.cpp`

- [ ] **Step 1: Write parser tests**

Create `test_provisioning_command/test_main.cpp`:

```cpp
#include <unity.h>
#include "../../src/ProvisioningCommand.h"

using namespace stopwatch;

void test_parseSetCommands(void) {
    ProvisioningCommand cmd;
    TEST_ASSERT_TRUE(parseProvisioningCommand("wifi ssid HomeNet", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetWiFiSSID, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("HomeNet", cmd.value);

    TEST_ASSERT_TRUE(parseProvisioningCommand("api base-url https://stopwatch.example.com", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetAPIBaseURL, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("https://stopwatch.example.com", cmd.value);

    TEST_ASSERT_TRUE(parseProvisioningCommand("api token abc123", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetAPIToken, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("abc123", cmd.value);
}

void test_parseShowAndClear(void) {
    ProvisioningCommand cmd;
    TEST_ASSERT_TRUE(parseProvisioningCommand("config show", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::ShowConfig, (int)cmd.action);

    TEST_ASSERT_TRUE(parseProvisioningCommand("config clear", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::ClearConfig, (int)cmd.action);
}

void test_rejectsUnknownOrEmptyCommands(void) {
    ProvisioningCommand cmd;
    TEST_ASSERT_FALSE(parseProvisioningCommand("", cmd));
    TEST_ASSERT_FALSE(parseProvisioningCommand("wifi", cmd));
    TEST_ASSERT_FALSE(parseProvisioningCommand("api nope x", cmd));
    TEST_ASSERT_FALSE(parseProvisioningCommand("config dump", cmd));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_parseSetCommands);
    RUN_TEST(test_parseShowAndClear);
    RUN_TEST(test_rejectsUnknownOrEmptyCommands);
    return UNITY_END();
}
```

- [ ] **Step 2: Run parser test and verify failure**

Run:

```bash
cd firmware && pio test -e native -f test_provisioning_command
```

Expected: fails because parser files do not exist.

- [ ] **Step 3: Add device config declarations**

Create `DeviceConfig.h`:

```cpp
#pragma once
#include <cstddef>

namespace stopwatch {

constexpr size_t kProvisioningValueMax = 192;

struct DeviceNetworkConfig {
    char wifiSSID[kProvisioningValueMax] = {};
    char wifiPassword[kProvisioningValueMax] = {};
    char apiBaseURL[kProvisioningValueMax] = {};
    char cfClientID[kProvisioningValueMax] = {};
    char cfClientSecret[kProvisioningValueMax] = {};
    char apiToken[kProvisioningValueMax] = {};

    bool wifiConfigured() const { return wifiSSID[0] != '\0' && wifiPassword[0] != '\0'; }
    bool apiConfigured() const {
        return apiBaseURL[0] != '\0' && cfClientID[0] != '\0' &&
               cfClientSecret[0] != '\0' && apiToken[0] != '\0';
    }
};

}  // namespace stopwatch
```

- [ ] **Step 4: Add parser header**

Create `ProvisioningCommand.h`:

```cpp
#pragma once
#include "DeviceConfig.h"

namespace stopwatch {

enum class ProvisioningAction {
    SetWiFiSSID,
    SetWiFiPassword,
    SetAPIBaseURL,
    SetCFAccessClientID,
    SetCFAccessClientSecret,
    SetAPIToken,
    ShowConfig,
    ClearConfig,
};

struct ProvisioningCommand {
    ProvisioningAction action = ProvisioningAction::ShowConfig;
    char value[kProvisioningValueMax] = {};
};

bool parseProvisioningCommand(const char *line, ProvisioningCommand &out);

}  // namespace stopwatch
```

- [ ] **Step 5: Implement parser**

Create `ProvisioningCommand.cpp`:

```cpp
#include "ProvisioningCommand.h"
#include <cstring>

namespace stopwatch {
namespace {

bool startsWith(const char *line, const char *prefix) {
    return std::strncmp(line, prefix, std::strlen(prefix)) == 0;
}

bool copyValue(const char *value, ProvisioningCommand &out) {
    if (!value || value[0] == '\0') return false;
    std::strncpy(out.value, value, sizeof(out.value) - 1);
    out.value[sizeof(out.value) - 1] = '\0';
    return true;
}

bool setWithValue(const char *line, const char *prefix,
                  ProvisioningAction action, ProvisioningCommand &out) {
    if (!startsWith(line, prefix)) return false;
    out.action = action;
    return copyValue(line + std::strlen(prefix), out);
}

}  // namespace

bool parseProvisioningCommand(const char *line, ProvisioningCommand &out) {
    if (!line || line[0] == '\0') return false;
    out.value[0] = '\0';

    if (setWithValue(line, "wifi ssid ", ProvisioningAction::SetWiFiSSID, out)) return true;
    if (setWithValue(line, "wifi password ", ProvisioningAction::SetWiFiPassword, out)) return true;
    if (setWithValue(line, "api base-url ", ProvisioningAction::SetAPIBaseURL, out)) return true;
    if (setWithValue(line, "api cf-client-id ", ProvisioningAction::SetCFAccessClientID, out)) return true;
    if (setWithValue(line, "api cf-client-secret ", ProvisioningAction::SetCFAccessClientSecret, out)) return true;
    if (setWithValue(line, "api token ", ProvisioningAction::SetAPIToken, out)) return true;

    if (std::strcmp(line, "config show") == 0) {
        out.action = ProvisioningAction::ShowConfig;
        return true;
    }
    if (std::strcmp(line, "config clear") == 0) {
        out.action = ProvisioningAction::ClearConfig;
        return true;
    }
    return false;
}

}  // namespace stopwatch
```

- [ ] **Step 6: Run parser tests**

Run:

```bash
cd firmware && pio test -e native -f test_provisioning_command
```

Expected: parser tests pass.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/DeviceConfig.h firmware/src/ProvisioningCommand.h firmware/src/ProvisioningCommand.cpp firmware/test/test_provisioning_command/test_main.cpp
git commit -m "firmware: parse provisioning commands"
```

---

### Task 8: Firmware Serial Provisioning Store

**Files:**
- Create: `firmware/src/SerialProvisioning.h`
- Create: `firmware/src/SerialProvisioning.cpp`
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Create serial provisioning API**

Create `SerialProvisioning.h`:

```cpp
#pragma once
#include "DeviceConfig.h"

namespace stopwatch {

class SerialProvisioning {
public:
    void begin();
    void poll();
    bool load(DeviceNetworkConfig &out);
    void clear();

private:
    void applyLine(const char *line);
    void printConfig();
    bool open_ = false;
};

}  // namespace stopwatch
```

- [ ] **Step 2: Implement Arduino-only provisioning**

Create `SerialProvisioning.cpp`:

```cpp
#include "SerialProvisioning.h"
#include "ProvisioningCommand.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <Preferences.h>
#endif

namespace stopwatch {

namespace {
constexpr const char *kNs = "swq-net";
constexpr const char *kSSID = "ssid";
constexpr const char *kPass = "pass";
constexpr const char *kBase = "base";
constexpr const char *kCFID = "cfid";
constexpr const char *kCFSecret = "cfsecret";
constexpr const char *kToken = "token";

#ifdef ARDUINO
Preferences prefs;
#endif
}

void SerialProvisioning::begin() {
#ifdef ARDUINO
    open_ = prefs.begin(kNs, false);
#endif
}

bool SerialProvisioning::load(DeviceNetworkConfig &out) {
#ifdef ARDUINO
    if (!open_) return false;
    prefs.getString(kSSID, out.wifiSSID, sizeof(out.wifiSSID));
    prefs.getString(kPass, out.wifiPassword, sizeof(out.wifiPassword));
    prefs.getString(kBase, out.apiBaseURL, sizeof(out.apiBaseURL));
    prefs.getString(kCFID, out.cfClientID, sizeof(out.cfClientID));
    prefs.getString(kCFSecret, out.cfClientSecret, sizeof(out.cfClientSecret));
    prefs.getString(kToken, out.apiToken, sizeof(out.apiToken));
    return true;
#else
    (void)out;
    return false;
#endif
}

void SerialProvisioning::clear() {
#ifdef ARDUINO
    if (open_) prefs.clear();
#endif
}

void SerialProvisioning::poll() {
#ifdef ARDUINO
    static char line[256];
    static size_t len = 0;
    while (Serial.available() > 0) {
        int b = Serial.read();
        if (b < 0) break;
        if (b == '\r') continue;
        if (b == '\n') {
            line[len] = '\0';
            applyLine(line);
            len = 0;
        } else if (len + 1 < sizeof(line)) {
            line[len++] = (char)b;
        }
    }
#endif
}

void SerialProvisioning::applyLine(const char *line) {
#ifdef ARDUINO
    ProvisioningCommand cmd;
    if (!parseProvisioningCommand(line, cmd)) {
        Serial.println("[provision] invalid command");
        return;
    }
    switch (cmd.action) {
        case ProvisioningAction::SetWiFiSSID: prefs.putString(kSSID, cmd.value); break;
        case ProvisioningAction::SetWiFiPassword: prefs.putString(kPass, cmd.value); break;
        case ProvisioningAction::SetAPIBaseURL: prefs.putString(kBase, cmd.value); break;
        case ProvisioningAction::SetCFAccessClientID: prefs.putString(kCFID, cmd.value); break;
        case ProvisioningAction::SetCFAccessClientSecret: prefs.putString(kCFSecret, cmd.value); break;
        case ProvisioningAction::SetAPIToken: prefs.putString(kToken, cmd.value); break;
        case ProvisioningAction::ShowConfig: printConfig(); return;
        case ProvisioningAction::ClearConfig: clear(); Serial.println("[provision] cleared"); return;
    }
    Serial.println("[provision] saved");
#endif
}

void SerialProvisioning::printConfig() {
#ifdef ARDUINO
    DeviceNetworkConfig cfg;
    load(cfg);
    Serial.printf("[provision] wifi_ssid=%s\n", cfg.wifiSSID[0] ? "set" : "missing");
    Serial.printf("[provision] wifi_password=%s\n", cfg.wifiPassword[0] ? "set" : "missing");
    Serial.printf("[provision] api_base_url=%s\n", cfg.apiBaseURL[0] ? cfg.apiBaseURL : "missing");
    Serial.printf("[provision] cf_client_id=%s len=%u\n", cfg.cfClientID[0] ? "set" : "missing", (unsigned)strlen(cfg.cfClientID));
    Serial.printf("[provision] cf_client_secret=%s len=%u\n", cfg.cfClientSecret[0] ? "set" : "missing", (unsigned)strlen(cfg.cfClientSecret));
    Serial.printf("[provision] api_token=%s len=%u\n", cfg.apiToken[0] ? "set" : "missing", (unsigned)strlen(cfg.apiToken));
#endif
}

}  // namespace stopwatch
```

- [ ] **Step 3: Wire into `main.cpp`**

Add global:

```cpp
stopwatch::SerialProvisioning g_provisioning;
```

In `setup()`, after `g_store.begin();`:

```cpp
g_provisioning.begin();
```

In `loop()`, immediately after `M5.update();`:

```cpp
g_provisioning.poll();
```

- [ ] **Step 4: Build firmware**

Run:

```bash
cd firmware && pio test -e native -f test_provisioning_command
cd firmware && pio run -e stopwatch
```

Expected: native parser tests pass and firmware compiles.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/SerialProvisioning.h firmware/src/SerialProvisioning.cpp firmware/src/main.cpp
git commit -m "firmware: store serial provisioning config"
```

---

### Task 9: Firmware Network Client

**Files:**
- Create: `firmware/src/NetworkClient.h`
- Create: `firmware/src/NetworkClient.cpp`
- Create: `firmware/src/TransportClient.h`
- Create: `firmware/src/TransportClient.cpp`
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Create NetworkClient API**

Create `NetworkClient.h`:

```cpp
#pragma once
#include "DeviceConfig.h"
#include <cstddef>
#include <cstdint>

namespace stopwatch {

class NetworkClient {
public:
    enum class FetchResult : uint8_t {
        Ok,
        WiFiMissing,
        APIMissing,
        WiFiOffline,
        AuthFailed,
        RequestFailed,
        BadPayload,
    };

    void begin();
    FetchResult fetchSnapshot(uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult fetchCost(uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult fetchBalances(uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult fetchUsage(uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult refresh(uint8_t scope);

private:
    FetchResult fetchPath(const char *path, uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult ensureWiFi(const DeviceNetworkConfig &cfg);
};

}  // namespace stopwatch
```

- [ ] **Step 2: Implement NetworkClient**

Create `NetworkClient.cpp`:

```cpp
#include "NetworkClient.h"
#include "SerialProvisioning.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#endif

namespace stopwatch {

namespace {
SerialProvisioning netStore;

bool appendURL(char *out, size_t n, const char *base, const char *path) {
    if (!base || !path || !out || n == 0) return false;
    size_t blen = strlen(base);
    bool slash = blen > 0 && base[blen - 1] == '/';
    int written = snprintf(out, n, "%s%s%s", base, slash ? "" : "/", path[0] == '/' ? path + 1 : path);
    return written > 0 && (size_t)written < n;
}
}

void NetworkClient::begin() {
    netStore.begin();
}

NetworkClient::FetchResult NetworkClient::ensureWiFi(const DeviceNetworkConfig &cfg) {
#ifdef ARDUINO
    if (!cfg.wifiConfigured()) return FetchResult::WiFiMissing;
    if (WiFi.status() == WL_CONNECTED) return FetchResult::Ok;
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSSID, cfg.wifiPassword);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && (uint32_t)(millis() - started) < 8000) {
        delay(100);
    }
    return WiFi.status() == WL_CONNECTED ? FetchResult::Ok : FetchResult::WiFiOffline;
#else
    (void)cfg;
    return FetchResult::WiFiOffline;
#endif
}

NetworkClient::FetchResult NetworkClient::fetchPath(const char *path, uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    outLen = 0;
    DeviceNetworkConfig cfg;
    netStore.load(cfg);
    if (!cfg.apiConfigured()) return FetchResult::APIMissing;
    FetchResult wifi = ensureWiFi(cfg);
    if (wifi != FetchResult::Ok) return wifi;

#ifdef ARDUINO
    char url[256];
    if (!appendURL(url, sizeof(url), cfg.apiBaseURL, path)) return FetchResult::APIMissing;
    HTTPClient http;
    if (!http.begin(url)) return FetchResult::RequestFailed;
    http.addHeader("CF-Access-Client-Id", cfg.cfClientID);
    http.addHeader("CF-Access-Client-Secret", cfg.cfClientSecret);
    char auth[224];
    snprintf(auth, sizeof(auth), "Bearer %s", cfg.apiToken);
    http.addHeader("Authorization", auth);
    int code = http.GET();
    if (code == 401 || code == 403) { http.end(); return FetchResult::AuthFailed; }
    if (code != 200) { http.end(); return FetchResult::RequestFailed; }
    int len = http.getSize();
    if (len <= 0 || (size_t)len > bufSize) { http.end(); return FetchResult::BadPayload; }
    WiFiClient *stream = http.getStreamPtr();
    size_t read = stream->readBytes(outBytes, len);
    http.end();
    if (read != (size_t)len) return FetchResult::BadPayload;
    outLen = read;
    return FetchResult::Ok;
#else
    (void)path; (void)outBytes; (void)bufSize;
    return FetchResult::RequestFailed;
#endif
}

NetworkClient::FetchResult NetworkClient::fetchSnapshot(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchPath("/v1/snapshot", outBytes, bufSize, outLen);
}

NetworkClient::FetchResult NetworkClient::fetchCost(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchPath("/v1/cost", outBytes, bufSize, outLen);
}

NetworkClient::FetchResult NetworkClient::fetchBalances(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchPath("/v1/balances", outBytes, bufSize, outLen);
}

NetworkClient::FetchResult NetworkClient::fetchUsage(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchPath("/v1/balance-usage", outBytes, bufSize, outLen);
}

NetworkClient::FetchResult NetworkClient::refresh(uint8_t scope) {
    uint8_t scratch[32];
    size_t len = 0;
    char path[32];
    snprintf(path, sizeof(path), "/v1/refresh?scope=%u", (unsigned)scope);
    return fetchPath(path, scratch, sizeof(scratch), len);
}

}  // namespace stopwatch
```

- [ ] **Step 3: Create transport facade**

Create `TransportClient.h`:

```cpp
#pragma once
#include "BleClient.h"
#include "CarouselSettings.h"
#include "NetworkClient.h"

namespace stopwatch {

class TransportClient {
public:
    void begin();
    BleClient &ble() { return ble_; }
    NetworkClient &network() { return net_; }

    NetworkClient::FetchResult fetchSnapshot(const CarouselSettings &settings, uint8_t scope,
                                             uint8_t *outBytes, size_t bufSize, size_t &outLen);
    NetworkClient::FetchResult fetchCost(const CarouselSettings &settings, uint8_t *outBytes, size_t bufSize, size_t &outLen);
    NetworkClient::FetchResult fetchBalances(const CarouselSettings &settings, uint8_t *outBytes, size_t bufSize, size_t &outLen);
    NetworkClient::FetchResult fetchUsage(const CarouselSettings &settings, uint8_t *outBytes, size_t bufSize, size_t &outLen);

private:
    BleClient ble_;
    NetworkClient net_;
};

}  // namespace stopwatch
```

Create `TransportClient.cpp`:

```cpp
#include "TransportClient.h"

namespace stopwatch {

void TransportClient::begin() {
    ble_.begin();
    net_.begin();
}

NetworkClient::FetchResult TransportClient::fetchSnapshot(const CarouselSettings &settings, uint8_t scope,
                                                          uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    if (settings.transportMode == TransportMode::WiFi) return net_.fetchSnapshot(outBytes, bufSize, outLen);
    auto rc = ble_.fetch(scope, outBytes, bufSize, outLen);
    return rc == BleClient::FetchResult::Ok ? NetworkClient::FetchResult::Ok :
           rc == BleClient::FetchResult::NoPeripheral ? NetworkClient::FetchResult::WiFiOffline :
           NetworkClient::FetchResult::RequestFailed;
}

NetworkClient::FetchResult TransportClient::fetchCost(const CarouselSettings &settings,
                                                      uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    if (settings.transportMode == TransportMode::WiFi) return net_.fetchCost(outBytes, bufSize, outLen);
    return ble_.fetchCost(outBytes, bufSize, outLen) == BleClient::FetchResult::Ok ?
        NetworkClient::FetchResult::Ok : NetworkClient::FetchResult::RequestFailed;
}

NetworkClient::FetchResult TransportClient::fetchBalances(const CarouselSettings &settings,
                                                          uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    if (settings.transportMode == TransportMode::WiFi) return net_.fetchBalances(outBytes, bufSize, outLen);
    return ble_.fetchBalances(outBytes, bufSize, outLen) == BleClient::FetchResult::Ok ?
        NetworkClient::FetchResult::Ok : NetworkClient::FetchResult::RequestFailed;
}

NetworkClient::FetchResult TransportClient::fetchUsage(const CarouselSettings &settings,
                                                       uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    if (settings.transportMode == TransportMode::WiFi) return net_.fetchUsage(outBytes, bufSize, outLen);
    return ble_.fetchUsage(outBytes, bufSize, outLen) == BleClient::FetchResult::Ok ?
        NetworkClient::FetchResult::Ok : NetworkClient::FetchResult::RequestFailed;
}

}  // namespace stopwatch
```

- [ ] **Step 4: Wire main to transport facade**

In `main.cpp`, replace the global `BleClient` with:

```cpp
stopwatch::TransportClient g_transport;
```

Replace `g_ble.begin();` with:

```cpp
g_transport.begin();
```

In `fetchAndApply`, replace:

```cpp
auto rc = g_ble.fetch(scope, buf, sizeof(buf), len);
```

with:

```cpp
auto rc = g_transport.fetchSnapshot(g_carouselSettings, scope, buf, sizeof(buf), len);
```

Map `NetworkClient::FetchResult` states to link status. Add new `LinkStatus` values in `App.h`:

```cpp
WiFiMissing,
APIMissing,
WiFiOffline,
APIAuth,
APIError,
```

Use this mapping in `fetchAndApply`:

```cpp
case stopwatch::NetworkClient::FetchResult::WiFiMissing:
    g_app.setLinkStatus(stopwatch::LinkStatus::WiFiMissing); return false;
case stopwatch::NetworkClient::FetchResult::APIMissing:
    g_app.setLinkStatus(stopwatch::LinkStatus::APIMissing); return false;
case stopwatch::NetworkClient::FetchResult::WiFiOffline:
    g_app.setLinkStatus(stopwatch::LinkStatus::WiFiOffline); return false;
case stopwatch::NetworkClient::FetchResult::AuthFailed:
    g_app.setLinkStatus(stopwatch::LinkStatus::APIAuth); return false;
case stopwatch::NetworkClient::FetchResult::RequestFailed:
case stopwatch::NetworkClient::FetchResult::BadPayload:
    g_app.setLinkStatus(stopwatch::LinkStatus::APIError); return false;
case stopwatch::NetworkClient::FetchResult::Ok:
    break;
```

Update `fetchCostAndApply`, `fetchBalancesAndApply`, and `fetchUsageAndApply` to call `g_transport`.

- [ ] **Step 5: Update platform dependencies**

No new PlatformIO dependency is needed for ESP32 `WiFi` and `HTTPClient`; they are included in the Arduino ESP32 framework.

- [ ] **Step 6: Build firmware**

Run:

```bash
cd firmware && pio test -e native
cd firmware && pio run -e stopwatch
```

Expected: native tests pass and stopwatch firmware compiles.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/NetworkClient.h firmware/src/NetworkClient.cpp firmware/src/TransportClient.h firmware/src/TransportClient.cpp firmware/src/main.cpp firmware/src/App.h
git commit -m "firmware: fetch snapshots over selected transport"
```

---

### Task 10: Bridge Direct Codex And Gemini Usage Collectors

**Files:**
- Create: `bridge/Sources/StopwatchBridge/ProviderCollectors/CodexUsageCollector.swift`
- Create: `bridge/Sources/StopwatchBridge/ProviderCollectors/GeminiUsageCollector.swift`
- Create: `bridge/Sources/StopwatchBridge/ProviderCollectors/DirectUsageCollector.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/CodexUsageCollectorTests.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/GeminiUsageCollectorTests.swift`

- [ ] **Step 1: Write Codex mapping tests**

Create `CodexUsageCollectorTests.swift`:

```swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct CodexUsageCollectorTests {
    @Test func mapsWhamUsageToNormalizedProvider() throws {
        let data = Data("""
        {
          "rate_limit": {
            "primary_window": {"used_percent": 28, "resets_at": "2026-06-02T12:00:00Z"},
            "secondary_window": {"used_percent": 59, "resets_at": "2026-06-06T12:00:00Z"}
          },
          "credits": {"balance": 112.4},
          "account": {"plan_type": "plus"}
        }
        """.utf8)
        let provider = try CodexUsageCollector.decodeUsageResponse(data, now: Date(timeIntervalSince1970: 1_780_000_000))
        #expect(provider.providerID == .codex)
        #expect(provider.sessionPct == 28)
        #expect(provider.weekPct == 59)
        #expect(provider.credits == 112.4)
        #expect(provider.plan == .plus)
    }
}
```

- [ ] **Step 2: Write Gemini mapping tests**

Create `GeminiUsageCollectorTests.swift`:

```swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct GeminiUsageCollectorTests {
    @Test func mapsQuotaBucketsToNormalizedProvider() throws {
        let data = Data("""
        {
          "quotaBuckets": [
            {"modelId": "gemini-2.5-pro", "remainingFraction": 0.42, "resetTime": "2026-06-02T12:00:00Z"},
            {"modelId": "gemini-2.5-flash", "remainingFraction": 0.88, "resetTime": "2026-06-03T12:00:00Z"}
          ],
          "tier": "free-tier"
        }
        """.utf8)
        let provider = try GeminiUsageCollector.decodeQuotaResponse(data)
        #expect(provider.providerID == .gemini)
        #expect(provider.sessionPct == 58)
        #expect(provider.weekPct == 12)
        #expect(provider.plan == .free)
    }
}
```

- [ ] **Step 3: Run collector tests and verify failure**

Run:

```bash
cd bridge && swift test --filter CodexUsageCollectorTests
cd bridge && swift test --filter GeminiUsageCollectorTests
```

Expected: fails because collectors do not exist.

- [ ] **Step 4: Implement Codex collector decode and fetch shell**

Create `CodexUsageCollector.swift` with:

```swift
import Foundation

public struct CodexUsageCollector: Sendable {
    public var session: URLSession
    public var authPath: URL

    public init(session: URLSession = .shared, authPath: URL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".codex/auth.json")) {
        self.session = session
        self.authPath = authPath
    }

    public static func decodeUsageResponse(_ data: Data, now: Date) throws -> NormalizedUsage.Provider {
        let raw = try JSONDecoder.iso8601.decode(RawWhamUsage.self, from: data)
        return .init(
            providerID: .codex,
            status: .ok,
            sessionPct: raw.rateLimit?.primaryWindow?.usedPercent.map(percentByte),
            weekPct: raw.rateLimit?.secondaryWindow?.usedPercent.map(percentByte),
            sessionResetAt: raw.rateLimit?.primaryWindow?.resetsAt,
            weekResetAt: raw.rateLimit?.secondaryWindow?.resetsAt,
            credits: raw.credits?.balance,
            plan: ProviderPlan(fromString: raw.account?.planType)
        )
    }

    public func fetch(now: Date = Date()) async throws -> NormalizedUsage.Provider {
        let token = try Self.loadAccessToken(authPath: authPath)
        var req = URLRequest(url: URL(string: "https://chatgpt.com/backend-api/wham/usage")!)
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        req.timeoutInterval = 20
        let (data, response) = try await session.data(for: req)
        if let http = response as? HTTPURLResponse, http.statusCode == 401 || http.statusCode == 403 {
            throw DirectCollectorError.auth
        }
        return try Self.decodeUsageResponse(data, now: now)
    }

    static func loadAccessToken(authPath: URL) throws -> String {
        let data = try Data(contentsOf: authPath)
        let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any]
        if let tokens = obj?["tokens"] as? [String: Any],
           let access = tokens["access_token"] as? String ?? tokens["accessToken"] as? String {
            return access
        }
        throw DirectCollectorError.auth
    }

    private static func percentByte(_ value: Double) -> UInt8 {
        UInt8(max(0, min(100, value.rounded())))
    }

    private struct RawWhamUsage: Decodable {
        var rateLimit: RateLimit?
        var credits: Credits?
        var account: Account?
        enum CodingKeys: String, CodingKey { case rateLimit = "rate_limit", credits, account }
        struct RateLimit: Decodable {
            var primaryWindow: Window?
            var secondaryWindow: Window?
            enum CodingKeys: String, CodingKey { case primaryWindow = "primary_window", secondaryWindow = "secondary_window" }
        }
        struct Window: Decodable {
            var usedPercent: Double?
            var resetsAt: Date?
            enum CodingKeys: String, CodingKey { case usedPercent = "used_percent", resetsAt = "resets_at" }
        }
        struct Credits: Decodable { var balance: Double? }
        struct Account: Decodable {
            var planType: String?
            enum CodingKeys: String, CodingKey { case planType = "plan_type" }
        }
    }
}

public enum DirectCollectorError: Error, Sendable {
    case auth
    case unavailable
}
```

- [ ] **Step 5: Implement Gemini decode and fetch shell**

Create `GeminiUsageCollector.swift`:

```swift
import Foundation

public struct GeminiUsageCollector: Sendable {
    public var session: URLSession
    public var credentialsPath: URL

    public init(session: URLSession = .shared, credentialsPath: URL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".gemini/oauth_creds.json")) {
        self.session = session
        self.credentialsPath = credentialsPath
    }

    public static func decodeQuotaResponse(_ data: Data) throws -> NormalizedUsage.Provider {
        let raw = try JSONDecoder.iso8601.decode(RawQuota.self, from: data)
        let pro = raw.quotaBuckets.filter { $0.modelId.lowercased().contains("pro") }
            .min { ($0.remainingFraction ?? 1) < ($1.remainingFraction ?? 1) }
        let flash = raw.quotaBuckets.filter { $0.modelId.lowercased().contains("flash") }
            .min { ($0.remainingFraction ?? 1) < ($1.remainingFraction ?? 1) }
        return .init(
            providerID: .gemini,
            status: .ok,
            sessionPct: pro?.remainingFraction.map { percentUsedFromRemaining($0) },
            weekPct: flash?.remainingFraction.map { percentUsedFromRemaining($0) },
            sessionResetAt: pro?.resetTime,
            weekResetAt: flash?.resetTime,
            credits: nil,
            plan: plan(from: raw.tier)
        )
    }

    public func fetch() async throws -> NormalizedUsage.Provider {
        let token = try Self.loadAccessToken(credentialsPath: credentialsPath)
        var req = URLRequest(url: URL(string: "https://cloudcode-pa.googleapis.com/v1internal:retrieveUserQuota")!)
        req.httpMethod = "POST"
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.httpBody = Data(#"{}"#.utf8)
        req.timeoutInterval = 20
        let (data, response) = try await session.data(for: req)
        if let http = response as? HTTPURLResponse, http.statusCode == 401 || http.statusCode == 403 {
            throw DirectCollectorError.auth
        }
        return try Self.decodeQuotaResponse(data)
    }

    static func loadAccessToken(credentialsPath: URL) throws -> String {
        let data = try Data(contentsOf: credentialsPath)
        let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any]
        guard let token = obj?["access_token"] as? String else { throw DirectCollectorError.auth }
        return token
    }

    private static func percentUsedFromRemaining(_ remaining: Double) -> UInt8 {
        UInt8(max(0, min(100, ((1 - remaining) * 100).rounded())))
    }

    private static func plan(from tier: String?) -> ProviderPlan {
        switch (tier ?? "").lowercased() {
        case "standard-tier": return .plus
        case "free-tier": return .free
        default: return .unknown
        }
    }

    private struct RawQuota: Decodable {
        var quotaBuckets: [Bucket]
        var tier: String?
        struct Bucket: Decodable {
            var modelId: String
            var remainingFraction: Double?
            var resetTime: Date?
        }
    }
}
```

- [ ] **Step 6: Add aggregate direct usage collector**

Create `DirectUsageCollector.swift`:

```swift
import Foundation

public actor DirectUsageCollector {
    private let codex: CodexUsageCollector
    private let gemini: GeminiUsageCollector

    public init(codex: CodexUsageCollector = .init(), gemini: GeminiUsageCollector = .init()) {
        self.codex = codex
        self.gemini = gemini
    }

    public func fetchAll(now: Date = Date()) async -> NormalizedUsage {
        var providers: [NormalizedUsage.Provider] = []
        if let codexProvider = try? await codex.fetch(now: now) { providers.append(codexProvider) }
        if let geminiProvider = try? await gemini.fetch() { providers.append(geminiProvider) }
        var flags: SnapshotFlags = []
        if providers.isEmpty { flags.insert(.providerMissing) }
        return NormalizedUsage(capturedAt: now, flags: flags, providers: providers)
    }
}
```

- [ ] **Step 7: Run collector tests**

Run:

```bash
cd bridge && swift test --filter CodexUsageCollectorTests
cd bridge && swift test --filter GeminiUsageCollectorTests
```

Expected: tests pass.

- [ ] **Step 8: Commit**

```bash
git add bridge/Sources/StopwatchBridge/ProviderCollectors bridge/Tests/StopwatchBridgeTests/CodexUsageCollectorTests.swift bridge/Tests/StopwatchBridgeTests/GeminiUsageCollectorTests.swift
git commit -m "bridge: add direct codex and gemini collectors"
```

---

### Task 11: Bridge Direct Claude Usage Collector

**Files:**
- Create: `bridge/Sources/StopwatchBridge/ProviderCollectors/ClaudeUsageCollector.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/ClaudeUsageCollectorTests.swift`

- [ ] **Step 1: Write Claude OAuth decode test**

Create `ClaudeUsageCollectorTests.swift`:

```swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct ClaudeUsageCollectorTests {
    @Test func mapsOAuthUsageToNormalizedProvider() throws {
        let data = Data("""
        {
          "five_hour": {"used_percent": 21, "resets_at": "2026-06-02T12:00:00Z"},
          "seven_day": {"used_percent": 65, "resets_at": "2026-06-08T12:00:00Z"},
          "subscription_type": "pro"
        }
        """.utf8)
        let provider = try ClaudeUsageCollector.decodeOAuthUsage(data)
        #expect(provider.providerID == .claude)
        #expect(provider.sessionPct == 21)
        #expect(provider.weekPct == 65)
        #expect(provider.plan == .pro)
    }
}
```

- [ ] **Step 2: Run test and verify failure**

Run:

```bash
cd bridge && swift test --filter ClaudeUsageCollectorTests
```

Expected: fails because `ClaudeUsageCollector` does not exist.

- [ ] **Step 3: Implement Claude OAuth collector**

Create `ClaudeUsageCollector.swift`:

```swift
import Foundation

public struct ClaudeUsageCollector: Sendable {
    public var session: URLSession
    public var credentialsPath: URL

    public init(session: URLSession = .shared, credentialsPath: URL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".claude/.credentials.json")) {
        self.session = session
        self.credentialsPath = credentialsPath
    }

    public static func decodeOAuthUsage(_ data: Data) throws -> NormalizedUsage.Provider {
        let raw = try JSONDecoder.iso8601.decode(RawOAuthUsage.self, from: data)
        return .init(
            providerID: .claude,
            status: .ok,
            sessionPct: raw.fiveHour?.usedPercent.map(percentByte),
            weekPct: raw.sevenDay?.usedPercent.map(percentByte),
            sessionResetAt: raw.fiveHour?.resetsAt,
            weekResetAt: raw.sevenDay?.resetsAt,
            credits: nil,
            plan: ProviderPlan(fromString: raw.subscriptionType)
        )
    }

    public func fetch() async throws -> NormalizedUsage.Provider {
        let token = try Self.loadAccessToken(credentialsPath: credentialsPath)
        var req = URLRequest(url: URL(string: "https://api.anthropic.com/api/oauth/usage")!)
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        req.setValue("oauth-2025-04-20", forHTTPHeaderField: "anthropic-beta")
        req.timeoutInterval = 20
        let (data, response) = try await session.data(for: req)
        if let http = response as? HTTPURLResponse, http.statusCode == 401 || http.statusCode == 403 {
            throw DirectCollectorError.auth
        }
        return try Self.decodeOAuthUsage(data)
    }

    static func loadAccessToken(credentialsPath: URL) throws -> String {
        let data = try Data(contentsOf: credentialsPath)
        let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any]
        if let token = obj?["access_token"] as? String { return token }
        if let token = obj?["accessToken"] as? String { return token }
        throw DirectCollectorError.auth
    }

    private static func percentByte(_ value: Double) -> UInt8 {
        UInt8(max(0, min(100, value.rounded())))
    }

    private struct RawOAuthUsage: Decodable {
        var fiveHour: Window?
        var sevenDay: Window?
        var subscriptionType: String?
        enum CodingKeys: String, CodingKey {
            case fiveHour = "five_hour"
            case sevenDay = "seven_day"
            case subscriptionType = "subscription_type"
        }
        struct Window: Decodable {
            var usedPercent: Double?
            var resetsAt: Date?
            enum CodingKeys: String, CodingKey {
                case usedPercent = "used_percent"
                case resetsAt = "resets_at"
            }
        }
    }
}
```

- [ ] **Step 4: Add Claude to direct aggregate collector**

Modify `DirectUsageCollector.swift`:

```swift
private let claude: ClaudeUsageCollector

public init(
    codex: CodexUsageCollector = .init(),
    claude: ClaudeUsageCollector = .init(),
    gemini: GeminiUsageCollector = .init())
{
    self.codex = codex
    self.claude = claude
    self.gemini = gemini
}
```

In `fetchAll`:

```swift
if let claudeProvider = try? await claude.fetch() { providers.append(claudeProvider) }
```

- [ ] **Step 5: Run Claude collector tests**

Run:

```bash
cd bridge && swift test --filter ClaudeUsageCollectorTests
```

Expected: tests pass.

- [ ] **Step 6: Commit**

```bash
git add bridge/Sources/StopwatchBridge/ProviderCollectors/ClaudeUsageCollector.swift bridge/Sources/StopwatchBridge/ProviderCollectors/DirectUsageCollector.swift bridge/Tests/StopwatchBridgeTests/ClaudeUsageCollectorTests.swift
git commit -m "bridge: add direct claude collector"
```

---

### Task 12: Bridge Direct Cost Collector

**Files:**
- Create directory: `bridge/Sources/StopwatchBridge/ProviderCollectors/CostUsage/`
- Create: `bridge/Sources/StopwatchBridge/ProviderCollectors/DirectCostCollector.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/DirectCostCollectorTests.swift`

- [ ] **Step 1: Copy narrow cost scanner files from CodexBar**

Copy these source files from `/Users/justinyan/Documents/github/CodexBar/Sources/CodexBarCore/` into `bridge/Sources/StopwatchBridge/ProviderCollectors/CostUsage/`:

```text
CostUsageFetcher.swift
CostUsageModels.swift
PiSessionCostScanner.swift
PiSessionCostCache.swift
Vendored/CostUsage/CostUsageCache.swift
Vendored/CostUsage/CostUsageJsonl.swift
Vendored/CostUsage/CostUsagePricing.swift
Vendored/CostUsage/CostUsageScanner.swift
Vendored/CostUsage/CostUsageScanner+CacheHelpers.swift
Vendored/CostUsage/CostUsageScanner+Claude.swift
Vendored/CostUsage/CostUsageScanner+CodexFastJSON.swift
Vendored/CostUsage/CostUsageScanner+CodexPriority.swift
Vendored/CostUsage/CostUsageScanner+CodexTruncatedPrefix.swift
Vendored/CostUsage/CostUsageScanner+Timestamp.swift
Vendored/CostUsage/ModelsDevPricing.swift
```

After copying, remove `CodexBarCore` imports. Do not copy CodexBar's full provider registry because it imports UI/cookie dependencies. Add this local provider enum in `bridge/Sources/StopwatchBridge/ProviderCollectors/CostUsage/UsageProvider.swift` so the copied scanner compiles:

```swift
import Foundation

public enum UsageProvider: String, CaseIterable, Sendable, Codable {
    case codex
    case claude
    case vertexai
    case bedrock
}
```

Keep the copied scanner private to the bridge target.

- [ ] **Step 2: Write direct cost collector test**

Create `DirectCostCollectorTests.swift`:

```swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct DirectCostCollectorTests {
    @Test func mapsTokenSnapshotToNormalizedCostProvider() {
        let token = CostUsageTokenSnapshot(
            sessionTokens: 100,
            sessionCostUSD: 1.25,
            last30DaysTokens: 1000,
            last30DaysCostUSD: 12.50,
            daily: [
                .init(
                    date: "2026-06-02",
                    inputTokens: 50,
                    outputTokens: 50,
                    totalTokens: 100,
                    costUSD: 1.25,
                    modelsUsed: ["gpt-5.5"],
                    modelBreakdowns: [
                        .init(modelName: "gpt-5.5", costUSD: 1.25, totalTokens: 100)
                    ])
            ],
            updatedAt: Date(timeIntervalSince1970: 1_780_000_000))

        let provider = DirectCostCollector.normalizedProvider(provider: .codex, snapshot: token, now: token.updatedAt)

        #expect(provider.providerID == .codex)
        #expect(provider.todayCostUSD == 1.25)
        #expect(provider.monthCostUSD == 12.50)
        #expect(provider.todayTokens == 100)
        #expect(provider.monthTokens == 1000)
        #expect(provider.models == ["gpt-5.5"])
    }
}
```

- [ ] **Step 3: Implement direct collector facade**

Create `DirectCostCollector.swift`:

```swift
import Foundation

enum DirectCostProvider: Sendable {
    case codex
    case claude

    var usageProvider: UsageProvider {
        switch self {
        case .codex: return .codex
        case .claude: return .claude
        }
    }

    var stopwatchProviderID: ProviderID {
        switch self {
        case .codex: return .codex
        case .claude: return .claude
        }
    }
}

public struct DirectCostCollector: Sendable {
    public init() {}

    public func fetchAll(now: Date = Date()) async -> NormalizedCost {
        var providers: [NormalizedCost.Provider] = []
        if let codex = try? await fetch(.codex, now: now) { providers.append(codex) }
        if let claude = try? await fetch(.claude, now: now) { providers.append(claude) }
        var flags: CostFlags = []
        if providers.isEmpty { flags.insert(.costUnavailable) }
        return NormalizedCost(capturedAt: now, flags: flags, providers: providers)
    }

    func fetch(_ provider: DirectCostProvider, now: Date) async throws -> NormalizedCost.Provider {
        let snapshot = try await loadSnapshot(provider: provider, now: now)
        return Self.normalizedProvider(provider: provider, snapshot: snapshot, now: now)
    }

    func loadSnapshot(provider: DirectCostProvider, now: Date) async throws -> CostUsageTokenSnapshot {
        try await CostUsageFetcher().loadTokenSnapshot(
            provider: provider.usageProvider,
            now: now,
            historyDays: Protocol.costHistoryDays,
            refreshPricingInBackground: false)
    }

    static func normalizedProvider(provider: DirectCostProvider, snapshot: CostUsageTokenSnapshot, now: Date) -> NormalizedCost.Provider {
        let history = CodexbarClient.alignDailyHistory(snapshot.daily.map { ($0.date, $0.costUSD ?? 0) },
                                                       anchor: now,
                                                       days: Protocol.costHistoryDays)
        let modelDays = snapshot.daily.map { day -> (date: String, models: [String: UInt64]) in
            var models: [String: UInt64] = [:]
            for breakdown in day.modelBreakdowns ?? [] {
                models[breakdown.modelName, default: 0] += UInt64(max(0, breakdown.totalTokens ?? 0))
            }
            if models.isEmpty {
                for model in day.modelsUsed ?? [] {
                    models[model, default: 0] += UInt64(max(0, day.totalTokens ?? 0))
                }
            }
            return (date: day.date, models: models)
        }
        return .init(
            providerID: provider.stopwatchProviderID,
            todayCostUSD: snapshot.sessionCostUSD,
            monthCostUSD: snapshot.last30DaysCostUSD,
            todayTokens: snapshot.sessionTokens.map { UInt64(max(0, $0)) },
            monthTokens: snapshot.last30DaysTokens.map { UInt64(max(0, $0)) },
            models: CodexbarClient.latestDayModelsByTokens(from: modelDays),
            history: history)
    }
}
```

- [ ] **Step 4: Run direct cost tests**

Run:

```bash
cd bridge && swift test --filter DirectCostCollectorTests
```

Expected: test passes after the facade compiles. Add focused scanner tests only for compile fixes that change parsing behavior.

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/ProviderCollectors/CostUsage bridge/Sources/StopwatchBridge/ProviderCollectors/DirectCostCollector.swift bridge/Tests/StopwatchBridgeTests/DirectCostCollectorTests.swift
git commit -m "bridge: add direct local cost collector"
```

---

### Task 13: Replace CodexBar Runtime Dependency In BridgeService

**Files:**
- Modify: `bridge/Sources/StopwatchBridge/BridgeService.swift`
- Modify: `bridge/Sources/StopwatchBridge/CodexbarSupervisor.swift` or leave unused
- Modify: `bridge/Sources/StopwatchBridge/CodexbarClient.swift` or leave only for tests during migration
- Test: `bridge/Tests/StopwatchBridgeTests/BridgeServiceTests.swift`

- [ ] **Step 1: Add BridgeService source-selection test helper**

In `BridgeServiceTests.swift`, add:

```swift
@Test func directCollectorsAreDefaultDataSource() {
    #expect(BridgeService.usesCodexbarRuntimeByDefault(spawnCodexbar: true) == false)
    #expect(BridgeService.usesCodexbarRuntimeByDefault(spawnCodexbar: false) == false)
}
```

- [ ] **Step 2: Add helper method**

In `BridgeService.swift`, add:

```swift
static func usesCodexbarRuntimeByDefault(spawnCodexbar: Bool) -> Bool {
    false
}
```

- [ ] **Step 3: Replace usage and cost refresh sources**

Add properties:

```swift
private let directUsageCollector = DirectUsageCollector()
private let directCostCollector = DirectCostCollector()
```

In `run()`, remove the `supervisor.start()` branch. Replace it with:

```swift
FileHandle.standardOutput.write(Data("codexbar runtime disabled; using direct collectors\n".utf8))
```

In `handleRefresh(scope:)`, replace:

```swift
let usage = try await client.fetch(scope: s)
```

with:

```swift
let usage = await directUsageCollector.fetchAll()
```

Remove the `do/catch` around `client.fetch` and treat empty/provider-missing output as a cache failure:

```swift
let bytes = snapshotCache.recordSuccess(usage)
await peripheral.updateSnapshot(bytes)
await repository.update(.snapshot, bytes: bytes)
let usageSucceeded = !usage.providers.isEmpty && !usage.flags.contains(.bridgeError)
```

In `handleCostRefresh`, replace:

```swift
let cost = try await client.fetchCost(scope: .all)
```

with:

```swift
let cost = await directCostCollector.fetchAll()
```

Use `costCache.recordSuccess(cost)` for non-empty or stale-unavailable output.

- [ ] **Step 4: Keep BLE and balance paths unchanged**

Do not remove `GATTPeripheral`, `BalanceClient`, or `UsageClient`. These are independent of CodexBar and still serve existing watch views.

- [ ] **Step 5: Run bridge tests**

Run:

```bash
cd bridge && swift test
```

Expected: bridge tests pass. Existing `CodexbarClientTests` can remain because the decoder is still useful for fixtures during migration; remove the runtime client only after all tests are updated.

- [ ] **Step 6: Commit**

```bash
git add bridge/Sources/StopwatchBridge/BridgeService.swift bridge/Tests/StopwatchBridgeTests/BridgeServiceTests.swift
git commit -m "bridge: use direct collectors by default"
```

---

### Task 14: README And Manual Validation

**Files:**
- Modify: `README.md`
- Modify: `Makefile` when the implementation adds a new helper command target

- [ ] **Step 1: Add Mac mini HTTP setup docs**

In `README.md`, add a section after install:

```markdown
## Mac mini WiFi mode

The bridge can serve the watch over HTTP while still keeping BLE as a manual fallback.
The helper binds to loopback and is intended to sit behind Cloudflare Tunnel:

```yaml
ingress:
  - hostname: stopwatch.example.com
    service: http://127.0.0.1:8787
  - service: http_status:404
```

Protect the hostname with a Cloudflare Access service-token policy. The watch sends
`CF-Access-Client-Id`, `CF-Access-Client-Secret`, and `Authorization: Bearer <api token>`.
```

- [ ] **Step 2: Add USB provisioning docs**

In `README.md`, add:

```markdown
Provision the watch over USB serial:

```text
wifi ssid MyNetwork
wifi password MyPassword
api base-url https://stopwatch.example.com
api cf-client-id <Cloudflare Access client id>
api cf-client-secret <Cloudflare Access client secret>
api token <bridge api token>
config show
```

`config show` redacts secrets. Use the on-watch local settings to switch
`Transport` between `WIFI` and `BLE`.
```

- [ ] **Step 3: Add troubleshooting states**

Update status pill docs with:

```markdown
- `wifi missing` - WiFi credentials have not been provisioned.
- `api missing` - API URL or auth values have not been provisioned.
- `wifi offline` - the watch cannot join WiFi.
- `api auth` - Cloudflare Access or bridge API token rejected the request.
- `api error` - HTTP request failed or returned malformed snapshot bytes.
```

- [ ] **Step 4: Run full verification**

Run:

```bash
make test
```

Expected:

- `cd bridge && swift test` passes.
- `cd firmware && pio test -e native` passes.
- firmware version bump script test passes.

- [ ] **Step 5: Manual smoke test**

Run locally:

```bash
make build
./bridge/.build/release/stopwatch-bridge serve-config
./bridge/.build/release/stopwatch-bridge pair
```

In another terminal, replace `<token>` with the printed token and check:

```bash
curl -i -H "Authorization: Bearer <token>" http://127.0.0.1:8787/v1/snapshot
curl -i -X POST -H "Authorization: Bearer <token>" "http://127.0.0.1:8787/v1/refresh?scope=0"
```

Expected:

- `/v1/snapshot` returns `HTTP/1.1 200 OK` and `Content-Type: application/octet-stream`.
- `/v1/refresh?scope=0` returns `HTTP/1.1 202 Accepted`.

- [ ] **Step 6: Commit**

```bash
git add README.md Makefile
git commit -m "docs: document wifi transport setup"
```

---

## Final Verification

Run these commands after all tasks:

```bash
git status --short
make test
cd firmware && pio run -e stopwatch
```

Expected:

- Only intentional files are dirty before any final commit.
- `make test` passes.
- Stopwatch firmware builds.

Manual device validation:

1. Flash firmware.
2. Provision WiFi/API values over USB serial.
3. Set local settings `Transport` to `WIFI`.
4. Confirm overview fetches through Cloudflare Tunnel.
5. Visit cost, balances, and balance detail views.
6. Set `Transport` to `BLE`.
7. Confirm BLE mode still fetches from local bridge.

## Plan Self-Review

Spec coverage:

- Manual transport mode: Tasks 6 and 9.
- USB serial provisioning: Tasks 7 and 8.
- Binary HTTP endpoints: Tasks 2, 3, 4, and 5.
- Nonblocking refresh scheduling: Task 4.
- Direct non-CodexBar provider sources: Tasks 10, 11, 12, and 13.
- All existing data types over HTTP: Tasks 2, 4, and 5.
- Cloudflare Access plus origin token: Tasks 3, 4, and 14.
- Testing and docs: each task has verification; Task 14 handles docs and final checks.

Placeholder scan:

- No unresolved stub directives or unowned "write tests" directives remain.
- The direct cost scanner task names the exact CodexBar files to copy and the bridge-facing facade to keep.

Type consistency:

- `SnapshotKind.balanceUsage` maps to `/v1/balance-usage`.
- `TransportMode::WiFi` and `TransportMode::BLE` are used consistently.
- `NetworkClient::FetchResult` is the firmware transport result type.
