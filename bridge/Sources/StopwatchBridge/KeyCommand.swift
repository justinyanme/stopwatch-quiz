// bridge/Sources/StopwatchBridge/KeyCommand.swift
import Foundation

enum KeyCommand {
    static func setKey(_ id: String) -> Int32 {
        FileHandle.standardError.write(Data("Paste the API key for '\(id)' and press Enter:\n".utf8))
        guard let secret = readLine(strippingNewline: true), !secret.isEmpty else {
            FileHandle.standardError.write(Data("no key read from stdin\n".utf8))
            return 1
        }
        do {
            try KeychainStore().set(secret, for: id)
            print("stored key for '\(id)' in the Keychain")
            return 0
        } catch {
            FileHandle.standardError.write(Data("failed to store key: \(error)\n".utf8))
            return 1
        }
    }

    static func listKeys() -> Int32 {
        let ids = KeychainStore().listIDs()
        if ids.isEmpty { print("no keys stored") } else { ids.forEach { print($0) } }
        return 0
    }

    static func deleteKey(_ id: String) -> Int32 {
        do { try KeychainStore().delete(id); print("deleted key for '\(id)'"); return 0 }
        catch { FileHandle.standardError.write(Data("failed: \(error)\n".utf8)); return 1 }
    }
}
