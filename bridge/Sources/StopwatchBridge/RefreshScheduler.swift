public actor RefreshScheduler {
    private let onRefresh: @Sendable (UInt8) async -> Void
    private var task: Task<Void, Never>?

    public init(onRefresh: @escaping @Sendable (UInt8) async -> Void) {
        self.onRefresh = onRefresh
    }

    public init(onRefresh: @escaping @Sendable (UInt8) -> Void) {
        self.onRefresh = { scope in onRefresh(scope) }
    }

    deinit {
        task?.cancel()
    }

    public func schedule(scope: UInt8) {
        task?.cancel()
        task = Task { [onRefresh] in
            await onRefresh(scope)
        }
    }
}
