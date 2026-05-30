// firmware/src/BleClient.cpp
#include "BleClient.h"
#include "Protocol.h"
#include <NimBLEDevice.h>
#include <Arduino.h>

namespace stopwatch {

void BleClient::begin() {
    NimBLEDevice::init("stopwatch");
}

BleClient::FetchResult BleClient::fetchInto(const char *charUuid, uint8_t scope,
                                            uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    outLen = 0;
    auto *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(45);
    scan->setWindow(15);

    NimBLEScanResults results = scan->getResults(2000);  // ms

    NimBLEUUID svcUuid(kServiceUUID);
    const NimBLEAdvertisedDevice *target = nullptr;
    int8_t bestRssi = -127;
    for (int i = 0; i < results.getCount(); ++i) {
        const NimBLEAdvertisedDevice *dev = results.getDevice(i);
        if (dev->isAdvertisingService(svcUuid) && dev->getRSSI() > bestRssi) {
            target = dev;
            bestRssi = dev->getRSSI();
        }
    }
    if (!target) return FetchResult::NoPeripheral;

    auto *client = NimBLEDevice::createClient();
    if (!client->connect(target)) {
        NimBLEDevice::deleteClient(client);
        return FetchResult::ConnectFailed;
    }

    auto tryReadOnce = [&]() -> FetchResult {
        auto *svc = client->getService(svcUuid);
        if (!svc) return FetchResult::ReadFailed;

        auto *trigger = svc->getCharacteristic(NimBLEUUID(kTriggerUUID));
        if (!trigger) return FetchResult::ReadFailed;
        uint8_t scopeBuf[1] = { scope };
        trigger->writeValue(scopeBuf, 1, /*response=*/false);

        delay(150);

        auto *ch = svc->getCharacteristic(NimBLEUUID(charUuid));
        if (!ch) return FetchResult::ReadFailed;
        NimBLEAttValue value = ch->readValue();
        if (value.size() == 0 || value.size() > bufSize) return FetchResult::ReadFailed;
        memcpy(outBytes, value.data(), value.size());
        outLen = value.size();
        return FetchResult::Ok;
    };

    FetchResult result = tryReadOnce();
    if (result == FetchResult::ReadFailed) {
        delay(100);
        result = tryReadOnce();
    }

    client->disconnect();
    NimBLEDevice::deleteClient(client);
    return result;
}

BleClient::FetchResult BleClient::fetch(uint8_t scope, uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchInto(kSnapshotUUID, scope, outBytes, bufSize, outLen);
}

BleClient::FetchResult BleClient::fetchCost(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchInto(kCostSnapshotUUID, kTriggerScopeCost, outBytes, bufSize, outLen);
}

BleClient::FetchResult BleClient::fetchBalances(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchInto(kBalanceSnapshotUUID, kTriggerScopeBalances, outBytes, bufSize, outLen);
}

BleClient::FetchResult BleClient::fetchUsage(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchInto(kUsageSnapshotUUID, kTriggerScopeUsage, outBytes, bufSize, outLen);
}

}  // namespace stopwatch
