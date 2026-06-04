public actor RefreshScheduler {
    private let onRefresh: @Sendable (UInt8) async -> Void
    private var task: Task<Void, Never>?
    private var generation: UInt64 = 0

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
        generation &+= 1
        let currentGeneration = generation
        task?.cancel()
        task = Task { [weak self, onRefresh] in
            await Task.yield()
            guard !Task.isCancelled,
                  await self?.isCurrentGeneration(currentGeneration) == true
            else {
                return
            }
            await onRefresh(scope)
        }
    }

    private func isCurrentGeneration(_ candidate: UInt64) -> Bool {
        generation == candidate
    }
}
