#include "TransportClient.h"

namespace stopwatch {

void TransportClient::begin() {
#ifdef ARDUINO
    ble_.begin();
#endif
    net_.begin();
}

NetworkClient::FetchResult TransportClient::mapBleResult(BleClient::FetchResult result) {
    switch (result) {
        case BleClient::FetchResult::Ok:
            return NetworkClient::FetchResult::Ok;
        case BleClient::FetchResult::NoPeripheral:
            return NetworkClient::FetchResult::WiFiOffline;
        case BleClient::FetchResult::ConnectFailed:
        case BleClient::FetchResult::ReadFailed:
            return NetworkClient::FetchResult::RequestFailed;
    }
    return NetworkClient::FetchResult::RequestFailed;
}

NetworkClient::FetchResult TransportClient::fetchSnapshot(
    const CarouselSettings &settings,
    uint8_t scope,
    uint8_t *outBytes,
    size_t bufSize,
    size_t &outLen)
{
    if (settings.transportMode == TransportMode::WiFi) {
        return net_.refreshAndFetchSnapshot(scope, outBytes, bufSize, outLen);
    }
#ifdef ARDUINO
    return mapBleResult(ble_.fetch(scope, outBytes, bufSize, outLen));
#else
    (void)scope;
    (void)outBytes;
    (void)bufSize;
    outLen = 0;
    return NetworkClient::FetchResult::RequestFailed;
#endif
}

NetworkClient::FetchResult TransportClient::fetchCost(
    const CarouselSettings &settings,
    uint8_t *outBytes,
    size_t bufSize,
    size_t &outLen)
{
    if (settings.transportMode == TransportMode::WiFi) {
        return net_.refreshAndFetchCost(outBytes, bufSize, outLen);
    }
#ifdef ARDUINO
    return mapBleResult(ble_.fetchCost(outBytes, bufSize, outLen));
#else
    (void)outBytes;
    (void)bufSize;
    outLen = 0;
    return NetworkClient::FetchResult::RequestFailed;
#endif
}

NetworkClient::FetchResult TransportClient::fetchBalances(
    const CarouselSettings &settings,
    uint8_t *outBytes,
    size_t bufSize,
    size_t &outLen)
{
    if (settings.transportMode == TransportMode::WiFi) {
        return net_.refreshAndFetchBalances(outBytes, bufSize, outLen);
    }
#ifdef ARDUINO
    return mapBleResult(ble_.fetchBalances(outBytes, bufSize, outLen));
#else
    (void)outBytes;
    (void)bufSize;
    outLen = 0;
    return NetworkClient::FetchResult::RequestFailed;
#endif
}

NetworkClient::FetchResult TransportClient::fetchUsage(
    const CarouselSettings &settings,
    uint8_t *outBytes,
    size_t bufSize,
    size_t &outLen)
{
    if (settings.transportMode == TransportMode::WiFi) {
        return net_.refreshAndFetchUsage(outBytes, bufSize, outLen);
    }
#ifdef ARDUINO
    return mapBleResult(ble_.fetchUsage(outBytes, bufSize, outLen));
#else
    (void)outBytes;
    (void)bufSize;
    outLen = 0;
    return NetworkClient::FetchResult::RequestFailed;
#endif
}

}  // namespace stopwatch
