import Testing
@testable import StopwatchBridge

@Suite struct BridgeServiceTests {
    @Test func refreshScopeNamesStayAlignedWithProtocol() {
        #expect(Protocol.triggerScopeCost == 0x04)
        #expect(Protocol.triggerScopeBalances == 0x05)
        #expect(Protocol.triggerScopeUsage == 0x06)
    }
}
