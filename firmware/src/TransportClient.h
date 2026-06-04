#pragma once
#include "BleClient.h"
#include "CarouselSettings.h"
#include "NetworkClient.h"

namespace stopwatch {

class TransportClient {
public:
    void begin();
    BleClient &ble() { return ble_; }
    NetworkClient &network() { return net_; }

    NetworkClient::FetchResult fetchSnapshot(const CarouselSettings &settings, uint8_t scope,
                                             uint8_t *outBytes, size_t bufSize, size_t &outLen);
    NetworkClient::FetchResult fetchCost(const CarouselSettings &settings,
                                         uint8_t *outBytes, size_t bufSize, size_t &outLen);
    NetworkClient::FetchResult fetchBalances(const CarouselSettings &settings,
                                             uint8_t *outBytes, size_t bufSize, size_t &outLen);
    NetworkClient::FetchResult fetchUsage(const CarouselSettings &settings,
                                          uint8_t *outBytes, size_t bufSize, size_t &outLen);

    static NetworkClient::FetchResult mapBleResult(BleClient::FetchResult result);

private:
    BleClient ble_;
    NetworkClient net_;
};

}  // namespace stopwatch
