import Testing
@testable import StopwatchBridge

@Suite struct BridgeServiceTests {
    @Test func failedFullUsageRefreshDoesNotCascadeToCostRefresh() {
        #expect(!BridgeService.shouldRefreshCostAfterUsage(scope: 0, usageSucceeded: false))
    }

    @Test func successfulFullUsageRefreshCascadesToCostRefresh() {
        #expect(BridgeService.shouldRefreshCostAfterUsage(scope: 0, usageSucceeded: true))
    }

    @Test func scopedUsageRefreshDoesNotCascadeToCostRefresh() {
        #expect(!BridgeService.shouldRefreshCostAfterUsage(scope: 1, usageSucceeded: true))
    }
}
