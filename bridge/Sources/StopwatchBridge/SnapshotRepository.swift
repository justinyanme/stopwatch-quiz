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
        self.snapshot = Self.isValid(snapshot, for: .snapshot) ? snapshot : SnapshotEncoder.staleEmpty()
        self.cost = Self.isValid(cost, for: .cost) ? cost : CostEncoder.staleEmpty()
        self.balances = Self.isValid(balances, for: .balances) ? balances : BalanceEncoder.staleEmpty()
        self.balanceUsage = Self.isValid(balanceUsage, for: .balanceUsage) ? balanceUsage : UsageEncoder.staleEmpty()
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
            return isValidVariableFrame(
                bytes,
                headerSize: Protocol.costHeaderSize,
                recordSize: Protocol.costRecordSize,
                versionMajor: Protocol.costVersionMajor,
                versionMinor: Protocol.costVersionMinor)
        case .balances:
            return isValidVariableFrame(
                bytes,
                headerSize: Protocol.balanceHeaderSize,
                recordSize: Protocol.balanceRecordSize,
                versionMajor: Protocol.balanceVersionMajor,
                versionMinor: Protocol.balanceVersionMinor,
                maxRecords: Protocol.balanceMaxRecords)
        case .balanceUsage:
            return isValidVariableFrame(
                bytes,
                headerSize: Protocol.usageHeaderSize,
                recordSize: Protocol.usageRecordSize,
                versionMajor: Protocol.usageVersionMajor,
                versionMinor: Protocol.usageVersionMinor,
                maxRecords: Protocol.usageMaxRecords)
        }
    }

    private static func isValidVariableFrame(
        _ bytes: Data,
        headerSize: Int,
        recordSize: Int,
        versionMajor: UInt8,
        versionMinor: UInt8,
        maxRecords: Int? = nil
    ) -> Bool {
        guard bytes.count >= headerSize else { return false }
        guard bytes[0] == versionMajor && bytes[1] == versionMinor else { return false }

        let recordCount = Int(bytes[2])
        if let maxRecords, recordCount > maxRecords {
            return false
        }

        return bytes.count == headerSize + recordSize * recordCount
    }
}
