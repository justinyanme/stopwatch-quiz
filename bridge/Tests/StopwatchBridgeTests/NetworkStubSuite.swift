import Testing

/// Parent suite that serializes all network-stub-using child suites, because
/// they share the process-global `BalanceStubURLProtocol.routes`. swift-testing
/// runs child suites of a `.serialized` suite one at a time.
@Suite(.serialized) struct NetworkStubTests {
    @Suite(.serialized) struct BalanceClientTests {}
    @Suite(.serialized) struct UsageClientTests {}
}
