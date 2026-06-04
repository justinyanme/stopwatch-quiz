#pragma once
#include <cstddef>

namespace stopwatch {

constexpr size_t kProvisioningValueMax = 192;

struct DeviceNetworkConfig {
    char wifiSSID[kProvisioningValueMax] = {};
    char wifiPassword[kProvisioningValueMax] = {};
    char apiBaseURL[kProvisioningValueMax] = {};
    char cfClientID[kProvisioningValueMax] = {};
    char cfClientSecret[kProvisioningValueMax] = {};
    char apiToken[kProvisioningValueMax] = {};

    bool wifiConfigured() const {
        return wifiSSID[0] != '\0' && wifiPassword[0] != '\0';
    }

    bool apiConfigured() const {
        return apiBaseURL[0] != '\0' && cfClientID[0] != '\0' &&
               cfClientSecret[0] != '\0' && apiToken[0] != '\0';
    }
};

}  // namespace stopwatch
