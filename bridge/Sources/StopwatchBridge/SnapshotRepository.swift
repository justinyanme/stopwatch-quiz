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
