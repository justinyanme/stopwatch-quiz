// bridge/Sources/StopwatchBridge/KeychainStore.swift
import Foundation
import Security

/// Read-only key lookup used by the balance fetcher. Implementations resolve a
/// provider id to its API key.
public protocol KeyStore: Sendable {
    func key(for id: String) -> String?
}

/// macOS Keychain-backed key storage. Items are generic passwords keyed by
/// (service, account=id), accessible after first unlock so the launchd daemon
/// can read them unattended.
public final class KeychainStore: KeyStore, @unchecked Sendable {
    public enum KeychainError: Error { case unavailable, osStatus(OSStatus) }

    private let service: String
    public init(service: String = "dev.stopwatch.bridge") { self.service = service }

    public func key(for id: String) -> String? {
        var query = baseQuery(id)
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne
        var item: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &item)
        guard status == errSecSuccess, let data = item as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    public func set(_ secret: String, for id: String) throws {
        try delete(id)
        var attrs = baseQuery(id)
        attrs[kSecValueData as String] = Data(secret.utf8)
        attrs[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlock
        let status = SecItemAdd(attrs as CFDictionary, nil)
        if status == errSecMissingEntitlement || status == errSecNotAvailable
            || status == errSecInteractionNotAllowed {
            throw KeychainError.unavailable
        }
        guard status == errSecSuccess else { throw KeychainError.osStatus(status) }
    }

    public func delete(_ id: String) throws {
        let status = SecItemDelete(baseQuery(id) as CFDictionary)
        if status == errSecSuccess || status == errSecItemNotFound { return }
        if status == errSecMissingEntitlement || status == errSecNotAvailable
            || status == errSecInteractionNotAllowed {
            throw KeychainError.unavailable
        }
        throw KeychainError.osStatus(status)
    }

    public func listIDs() -> [String] {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecReturnAttributes as String: true,
            kSecMatchLimit as String: kSecMatchLimitAll,
        ]
        var items: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &items) == errSecSuccess,
              let arr = items as? [[String: Any]] else { return [] }
        return arr.compactMap { $0[kSecAttrAccount as String] as? String }.sorted()
    }

    private func baseQuery(_ id: String) -> [String: Any] {
        [ kSecClass as String: kSecClassGenericPassword,
          kSecAttrService as String: service,
          kSecAttrAccount as String: id ]
    }
}

/// In-memory KeyStore for tests.
public final class FakeKeyStore: KeyStore, @unchecked Sendable {
    private var store: [String: String]
    private let lock = NSLock()
    public init(_ initial: [String: String] = [:]) { store = initial }
    public func key(for id: String) -> String? { lock.lock(); defer { lock.unlock() }; return store[id] }
    public func set(_ s: String, for id: String) { lock.lock(); store[id] = s; lock.unlock() }
    public func delete(_ id: String) { lock.lock(); store[id] = nil; lock.unlock() }
}
