// firmware/src/BleClient.cpp
#include "BleClient.h"
#include "Protocol.h"
#include <NimBLEDevice.h>
#include <Arduino.h>

namespace stopwatch {

namespace {
uint32_t readU32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

bool isPendingUsagePayload(const NimBLEAttValue &value) {
    if (value.size() != kUsageHeaderSize) return false;
    const uint8_t *b = value.data();
    const uint8_t flags = b[3];
    return b[0] <= kUsageVersionMajor &&
           b[2] == 0 &&
           (flags & kUsageFlagStale) &&
           (flags & kUsageFlagUnavailable) &&
           readU32(b + 4) == 0;
}
}  // namespace

void BleClient::begin() {
    NimBLEDevice::init("stopwatch");
}

BleClient::FetchResult BleClient::fetchInto(const char *charUuid, uint8_t scope,
                                            uint8_t *outBytes, size_t bufSize, size_t &outLen,
                                            bool pollPastPendingUsage) {
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

        const uint32_t started = millis();
        FetchResult last = FetchResult::ReadFailed;
        do {
            NimBLEAttValue value = ch->readValue();
            if (value.size() != 0 && value.size() <= bufSize) {
                memcpy(outBytes, value.data(), value.size());
                outLen = value.size();
                last = FetchResult::Ok;
                if (!pollPastPendingUsage || !isPendingUsagePayload(value)) return FetchResult::Ok;
            }
            if (!pollPastPendingUsage) break;
            delay(350);
        } while ((uint32_t)(millis() - started) < 6000);

        return last;
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
    return fetchInto(kUsageSnapshotUUID, kTriggerScopeUsage, outBytes, bufSize, outLen,
                     /*pollPastPendingUsage=*/true);
}

}  // namespace stopwatch
