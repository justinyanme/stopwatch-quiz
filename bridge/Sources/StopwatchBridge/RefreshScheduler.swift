public actor RefreshScheduler {
    private let onRefresh: @Sendable (UInt8) -> Void

    public init(onRefresh: @escaping @Sendable (UInt8) -> Void) {
        self.onRefresh = onRefresh
    }

    public nonisolated func schedule(scope: UInt8) {
        onRefresh(scope)
    }
}
