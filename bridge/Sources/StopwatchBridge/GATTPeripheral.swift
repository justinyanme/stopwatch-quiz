@preconcurrency import CoreBluetooth
import Foundation

public protocol GATTPeripheralDelegate: AnyObject, Sendable {
    /// Called when the watch writes to `RefreshTrigger`. The supervisor should
    /// re-fetch and then call `updateSnapshot(_:)` on this peripheral.
    func gattPeripheral(_ peripheral: GATTPeripheral, refreshRequestedFor scope: UInt8) async
}

public final class GATTPeripheral: NSObject, @unchecked Sendable {

    public weak var delegate: (any GATTPeripheralDelegate)?

    private let manager: CBPeripheralManager
    private let snapshotChar: CBMutableCharacteristic
    private let triggerChar:  CBMutableCharacteristic
    private let service:      CBMutableService

    private var currentSnapshot: Data = Data(count: Protocol.snapshotSize)
    private var isAdvertising = false

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
        let svc = CBMutableService(type: Protocol.serviceUUID, primary: true)
        svc.characteristics = [self.snapshotChar, self.triggerChar]
        self.service = svc

        self.manager = CBPeripheralManager(delegate: nil, queue: nil)
        super.init()
        self.manager.delegate = self
    }

    public func updateSnapshot(_ data: Data) {
        precondition(data.count == Protocol.snapshotSize, "snapshot must be \(Protocol.snapshotSize) bytes")
        currentSnapshot = data
        snapshotChar.value = data
        _ = manager.updateValue(data, for: snapshotChar, onSubscribedCentrals: nil)
    }
}

extension GATTPeripheral: CBPeripheralManagerDelegate {

    public func peripheralManagerDidUpdateState(_ peripheral: CBPeripheralManager) {
        guard peripheral.state == .poweredOn else {
            FileHandle.standardError.write(Data("Bluetooth not powered on (state=\(peripheral.state.rawValue))\n".utf8))
            return
        }
        if !isAdvertising {
            peripheral.add(service)
            peripheral.startAdvertising([
                CBAdvertisementDataLocalNameKey: Protocol.localName,
                CBAdvertisementDataServiceUUIDsKey: [Protocol.serviceUUID]
            ])
            isAdvertising = true
            FileHandle.standardOutput.write(Data("advertising \(Protocol.localName)\n".utf8))
        }
    }

    public func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveRead request: CBATTRequest) {
        guard request.characteristic.uuid == Protocol.snapshotUUID else {
            peripheral.respond(to: request, withResult: .attributeNotFound)
            return
        }
        if request.offset > currentSnapshot.count {
            peripheral.respond(to: request, withResult: .invalidOffset)
            return
        }
        request.value = currentSnapshot.subdata(in: request.offset..<currentSnapshot.count)
        peripheral.respond(to: request, withResult: .success)
    }

    public func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveWrite requests: [CBATTRequest]) {
        for req in requests {
            guard req.characteristic.uuid == Protocol.triggerUUID,
                  let v = req.value,
                  v.count >= 1
            else { continue }
            let scope = v[0]
            FileHandle.standardOutput.write(Data("trigger write: scope=\(scope)\n".utf8))
            Task { await self.delegate?.gattPeripheral(self, refreshRequestedFor: scope) }
        }
    }
}
