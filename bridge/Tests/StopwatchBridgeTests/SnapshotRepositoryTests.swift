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
