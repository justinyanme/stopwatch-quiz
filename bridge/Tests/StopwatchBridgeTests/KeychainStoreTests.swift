import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct KeychainStoreTests {

    @Test func fakeStoreRoundTrips() {
        let fake = FakeKeyStore()
        #expect(fake.key(for: "openrouter") == nil)
        fake.set("sk-abc", for: "openrouter")
        #expect(fake.key(for: "openrouter") == "sk-abc")
        fake.delete("openrouter")
        #expect(fake.key(for: "openrouter") == nil)
    }

    @Test func keychainRoundTripsWhenAvailable() throws {
        let store = KeychainStore(service: "dev.stopwatch.bridge.test-\(UUID().uuidString)")
        // Some CI sandboxes have no usable keychain (errSecMissingEntitlement); skip there.
        do {
            try store.set("sk-live", for: "deepseek")
        } catch KeychainStore.KeychainError.unavailable {
            return  // environment without a keychain; nothing to assert
        }
        #expect(store.key(for: "deepseek") == "sk-live")
        try store.delete("deepseek")
        #expect(store.key(for: "deepseek") == nil)
    }
}
