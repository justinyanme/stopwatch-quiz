// bridge/Sources/StopwatchBridge/GATTPeripheral.swift
@preconcurrency import CoreBluetooth
import Foundation

public protocol GATTPeripheralDelegate: AnyObject, Sendable {
    /// Called when the watch writes to `RefreshTrigger`. The supervisor should
    /// re-fetch and then call `updateSnapshot(_:)` on this peripheral.
    func gattPeripheral(_ peripheral: GATTPeripheral, refreshRequestedFor scope: UInt8) async
}

/// CoreBluetooth peripheral that advertises the stopwatch service and serves
/// the latest binary snapshot. Main-actor-isolated because CBPeripheralManager
/// delivers all delegate callbacks on the main queue when initialized with
/// `queue: nil`.
@MainActor
public final class GATTPeripheral: NSObject {

    public weak var delegate: (any GATTPeripheralDelegate)?

    private let manager: CBPeripheralManager
    private let snapshotChar: CBMutableCharacteristic
    private let triggerChar:  CBMutableCharacteristic
    private let costChar:     CBMutableCharacteristic
    private let balanceChar:  CBMutableCharacteristic
    private let service:      CBMutableService

    /// Initialised to a well-formed v1.0 stale-empty snapshot so a watch reading
    /// before the first real update doesn't see a zero header.
    private var currentSnapshot: Data = SnapshotEncoder.staleEmpty()
    private var isAdvertising = false
    private var pendingNotify: Data?     // last data that updateValue refused (queue full); resent on isReadyToUpdate
    private var currentCost: Data = CostEncoder.staleEmpty()
    private var pendingCostNotify: Data?
    private var currentBalance: Data = BalanceEncoder.staleEmpty()
    private var pendingBalanceNotify: Data?
    private var refreshTask: Task<Void, Never>?  // serializes trigger writes — newer cancels older

    public override init() {
        self.snapshotChar = CBMutableCharacteristic(
            type: Protocol.snapshotUUID,
            properties: [.read, .notify],
            value: nil,
            permissions: [.readable]
        )
        self.triggerChar = CBMutableCharacteristic(
            type: Protocol.triggerUUID,
            properties: [.writeWithoutResponse],
            value: nil,
            permissions: [.writeable]
        )
        self.costChar = CBMutableCharacteristic(
            type: Protocol.costSnapshotUUID,
            properties: [.read, .notify],
            value: nil,
            permissions: [.readable]
        )
        self.balanceChar = CBMutableCharacteristic(
            type: Protocol.balanceSnapshotUUID,
            properties: [.read, .notify],
            value: nil,
            permissions: [.readable]
        )
        let svc = CBMutableService(type: Protocol.serviceUUID, primary: true)
        svc.characteristics = [self.snapshotChar, self.triggerChar, self.costChar, self.balanceChar]
        self.service = svc

        self.manager = CBPeripheralManager(delegate: nil, queue: nil)
        super.init()
        self.manager.delegate = self
    }

    public func updateSnapshot(_ data: Data) {
        precondition(data.count == Protocol.snapshotSize, "snapshot must be \(Protocol.snapshotSize) bytes")
        currentSnapshot = data
        if !manager.updateValue(data, for: snapshotChar, onSubscribedCentrals: nil) {
            // Transmit queue full; remember and resend when CoreBluetooth says it's ready.
            pendingNotify = data
        } else {
            pendingNotify = nil
        }
    }

    public func updateCostSnapshot(_ data: Data) {
        precondition(data.count >= Protocol.costHeaderSize, "cost snapshot too short")
        currentCost = data
        if !manager.updateValue(data, for: costChar, onSubscribedCentrals: nil) {
            pendingCostNotify = data
        } else {
            pendingCostNotify = nil
        }
    }

    public func updateBalanceSnapshot(_ data: Data) {
        precondition(data.count >= Protocol.balanceHeaderSize, "balance snapshot too short")
        currentBalance = data
        if !manager.updateValue(data, for: balanceChar, onSubscribedCentrals: nil) {
            pendingBalanceNotify = data
        } else {
            pendingBalanceNotify = nil
        }
    }
}

extension GATTPeripheral: CBPeripheralManagerDelegate {

    nonisolated public func peripheralManagerDidUpdateState(_ peripheral: CBPeripheralManager) {
        // CoreBluetooth invokes us on main when queue: nil, but the protocol decl is nonisolated.
        // Hop to MainActor explicitly to keep all state mutations on the same isolation.
        Task { @MainActor in self.handleStateChange(peripheral: peripheral) }
    }

    nonisolated public func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveRead request: CBATTRequest) {
        Task { @MainActor in self.handleRead(peripheral: peripheral, request: request) }
    }

    nonisolated public func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveWrite requests: [CBATTRequest]) {
        // [CBATTRequest] isn't Sendable, so extract the trigger scopes here on the
        // calling queue and forward a Sendable [UInt8] to the main actor.
        let triggerScopes: [UInt8] = requests.compactMap { req in
            guard req.characteristic.uuid == Protocol.triggerUUID,
                  let v = req.value,
                  v.count >= 1 else { return nil }
            return v[0]
        }
        guard !triggerScopes.isEmpty else { return }
        Task { @MainActor in self.handleTriggerScopes(triggerScopes) }
    }

    nonisolated public func peripheralManagerIsReady(toUpdateSubscribers peripheral: CBPeripheralManager) {
        Task { @MainActor in self.flushPendingNotify(peripheral: peripheral) }
    }

    nonisolated public func peripheralManager(_ peripheral: CBPeripheralManager,
                                              didAdd service: CBService, error: Error?) {
        Task { @MainActor in self.handleDidAdd(peripheral: peripheral, error: error) }
    }

    // MARK: - MainActor-isolated handlers

    private func handleStateChange(peripheral: CBPeripheralManager) {
        guard peripheral.state == .poweredOn else {
            FileHandle.standardError.write(Data("Bluetooth not powered on (state=\(peripheral.state.rawValue)); pausing advertising\n".utf8))
            // Reset so we re-advertise cleanly when Bluetooth comes back.
            isAdvertising = false
            return
        }
        if !isAdvertising {
            // Idempotent re-arming: removeAllServices() is safe even on first run.
            // startAdvertising() fires from didAdd(_:error:) after CoreBluetooth
            // confirms the service registered — failure surfaces with a log instead
            // of silently advertising an empty service.
            peripheral.removeAllServices()
            peripheral.add(service)
        }
    }

    /// Periodic backstop, driven by `BridgeService`'s prewarm loop. CoreBluetooth
    /// silently pauses a foreground/CLI process's advertising across a system
    /// sleep and does NOT re-fire `peripheralManagerDidUpdateState` on wake
    /// (Bluetooth stays `.poweredOn`), so `handleStateChange` never re-arms and
    /// the watch starts seeing "no bridge". Worse, our own `isAdvertising` flag
    /// stays `true` because no callback fired — so we must poll the *real*
    /// `CBPeripheralManager.isAdvertising` and rebuild the service if it dropped.
    func ensureAdvertising() {
        guard manager.state == .poweredOn else { return }
        guard !manager.isAdvertising else { return }
        FileHandle.standardOutput.write(Data("advertising stopped (post-sleep?); re-arming GATT service\n".utf8))
        isAdvertising = false
        manager.removeAllServices()   // didAdd(_:error:) re-fires startAdvertising
        manager.add(service)
    }

    private func handleDidAdd(peripheral: CBPeripheralManager, error: Error?) {
        if let error = error {
            FileHandle.standardError.write(
                Data("GATT service add failed: \(error); will retry on next BT state change\n".utf8))
            isAdvertising = false  // force re-attempt on next poweredOn
            return
        }
        peripheral.startAdvertising([
            CBAdvertisementDataLocalNameKey: Protocol.localName,
            CBAdvertisementDataServiceUUIDsKey: [Protocol.serviceUUID]
        ])
        isAdvertising = true
        FileHandle.standardOutput.write(Data("advertising \(Protocol.localName)\n".utf8))
    }

    private func handleRead(peripheral: CBPeripheralManager, request: CBATTRequest) {
        let source: Data
        switch request.characteristic.uuid {
        case Protocol.snapshotUUID:     source = currentSnapshot
        case Protocol.costSnapshotUUID:    source = currentCost
        case Protocol.balanceSnapshotUUID: source = currentBalance
        default:
            peripheral.respond(to: request, withResult: .attributeNotFound)
            return
        }
        if request.offset >= source.count {
            peripheral.respond(to: request, withResult: .invalidOffset)
            return
        }
        request.value = source.subdata(in: request.offset..<source.count)
        peripheral.respond(to: request, withResult: .success)
    }

    private func handleTriggerScopes(_ scopes: [UInt8]) {
        for scope in scopes {
            FileHandle.standardOutput.write(Data("trigger write: scope=\(scope)\n".utf8))
            // Serialize trigger handling — a newer write supersedes an in-flight one.
            refreshTask?.cancel()
            refreshTask = Task { [weak self] in
                guard let self else { return }
                await self.delegate?.gattPeripheral(self, refreshRequestedFor: scope)
            }
        }
    }

    private func flushPendingNotify(peripheral: CBPeripheralManager) {
        if let pending = pendingNotify,
           peripheral.updateValue(pending, for: snapshotChar, onSubscribedCentrals: nil) {
            pendingNotify = nil
        }
        if let pendingCost = pendingCostNotify,
           peripheral.updateValue(pendingCost, for: costChar, onSubscribedCentrals: nil) {
            pendingCostNotify = nil
        }
        if let pendingBalance = pendingBalanceNotify,
           peripheral.updateValue(pendingBalance, for: balanceChar, onSubscribedCentrals: nil) {
            pendingBalanceNotify = nil
        }
    }
}
