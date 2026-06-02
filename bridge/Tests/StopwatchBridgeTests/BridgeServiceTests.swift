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
}
