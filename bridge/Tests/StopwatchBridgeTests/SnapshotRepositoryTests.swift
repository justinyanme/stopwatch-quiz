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

    @Test func storesAndReturnsUpdatedCostFrame() async {
        let repo = SnapshotRepository()
        let cost = CostEncoder.encode(.costFixtureTwo)
        await repo.update(.cost, bytes: cost)
        #expect(await repo.bytes(for: .cost) == cost)
    }

    @Test func storesAndReturnsUpdatedBalancesFrame() async {
        let repo = SnapshotRepository()
        let balances = BalanceEncoder.encode(.balanceFixtureTwo)
        await repo.update(.balances, bytes: balances)
        #expect(await repo.bytes(for: .balances) == balances)
    }

    @Test func storesAndReturnsUpdatedBalanceUsageFrame() async {
        let repo = SnapshotRepository()
        let usage = UsageEncoder.encode(.openRouterFixture)
        await repo.update(.balanceUsage, bytes: usage)
        #expect(await repo.bytes(for: .balanceUsage) == usage)
    }

    @Test func rejectsHeaderOnlyCostFrameWithRecordCount() async {
        let repo = SnapshotRepository()
        let before = await repo.bytes(for: .cost)
        await repo.update(.cost, bytes: headerOnlyCostFrame(recordCount: 1))
        #expect(await repo.bytes(for: .cost) == before)
    }

    @Test func rejectsHeaderOnlyBalancesFrameWithRecordCount() async {
        let repo = SnapshotRepository()
        let before = await repo.bytes(for: .balances)
        await repo.update(.balances, bytes: headerOnlyBalancesFrame(recordCount: 1))
        #expect(await repo.bytes(for: .balances) == before)
    }

    @Test func rejectsHeaderOnlyBalanceUsageFrameWithRecordCount() async {
        let repo = SnapshotRepository()
        let before = await repo.bytes(for: .balanceUsage)
        await repo.update(.balanceUsage, bytes: headerOnlyBalanceUsageFrame(recordCount: 1))
        #expect(await repo.bytes(for: .balanceUsage) == before)
    }

    @Test func invalidCustomInitialFramesFallBackToStaleEmptyFrames() async {
        let repo = SnapshotRepository(
            cost: headerOnlyCostFrame(recordCount: 1),
            balances: headerOnlyBalancesFrame(recordCount: 1),
            balanceUsage: headerOnlyBalanceUsageFrame(recordCount: 1))

        #expect(await repo.bytes(for: .cost) == CostEncoder.staleEmpty())
        #expect(await repo.bytes(for: .balances) == BalanceEncoder.staleEmpty())
        #expect(await repo.bytes(for: .balanceUsage) == UsageEncoder.staleEmpty())
    }

    private func headerOnlyCostFrame(recordCount: UInt8) -> Data {
        var bytes = Data(repeating: 0, count: Protocol.costHeaderSize)
        bytes[0] = Protocol.costVersionMajor
        bytes[1] = Protocol.costVersionMinor
        bytes[2] = recordCount
        return bytes
    }

    private func headerOnlyBalancesFrame(recordCount: UInt8) -> Data {
        var bytes = Data(repeating: 0, count: Protocol.balanceHeaderSize)
        bytes[0] = Protocol.balanceVersionMajor
        bytes[1] = Protocol.balanceVersionMinor
        bytes[2] = recordCount
        return bytes
    }

    private func headerOnlyBalanceUsageFrame(recordCount: UInt8) -> Data {
        var bytes = Data(repeating: 0, count: Protocol.usageHeaderSize)
        bytes[0] = Protocol.usageVersionMajor
        bytes[1] = Protocol.usageVersionMinor
        bytes[2] = recordCount
        return bytes
    }
}
