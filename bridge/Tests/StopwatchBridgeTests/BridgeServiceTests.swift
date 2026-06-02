import Testing
@testable import StopwatchBridge

@Suite struct BridgeServiceTests {
    @Test func refreshScopeNamesStayAlignedWithProtocol() {
        #expect(Protocol.triggerScopeCost == 0x04)
        #expect(Protocol.triggerScopeBalances == 0x05)
        #expect(Protocol.triggerScopeUsage == 0x06)
    }

    @Test func cancelledRefreshResultIsNotPublishable() async throws {
        let task = Task {
            await Task.yield()
            return BridgeService.shouldPublishRefreshResult()
        }
        task.cancel()

        let publish = await task.value
        #expect(!publish)
        #expect(BridgeService.shouldPublishRefreshResult(taskIsCancelled: false))
        #expect(!BridgeService.shouldPublishRefreshResult(taskIsCancelled: true))
    }

    @Test func directCollectorsAreDefaultDataSource() {
        #expect(BridgeService.usesCodexbarRuntimeByDefault(spawnCodexbar: true) == false)
        #expect(BridgeService.usesCodexbarRuntimeByDefault(spawnCodexbar: false) == false)
    }

    @Test func httpServerEventMessagesReflectActualLifecycle() {
        let listening = BridgeService.httpServerEventMessage(
            .listening(host: "127.0.0.1", port: 8787))
        #expect(listening.stream == .standardOutput)
        #expect(listening.text == "http listening on http://127.0.0.1:8787\n")

        let failed = BridgeService.httpServerEventMessage(
            .failed(host: "127.0.0.1", port: 8787, message: "Address already in use"))
        #expect(failed.stream == .standardError)
        #expect(failed.text == "http server failed on http://127.0.0.1:8787: Address already in use\n")
    }
}
