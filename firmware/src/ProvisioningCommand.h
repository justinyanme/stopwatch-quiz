#pragma once
#include "DeviceConfig.h"

namespace stopwatch {

enum class ProvisioningAction {
    SetWiFiSSID,
    SetWiFiPassword,
    SetAPIBaseURL,
    SetCFAccessClientID,
    SetCFAccessClientSecret,
    SetAPIToken,
    ShowConfig,
    ClearConfig,
};

struct ProvisioningCommand {
    ProvisioningAction action = ProvisioningAction::ShowConfig;
    char value[kProvisioningValueMax] = {};
};

bool parseProvisioningCommand(const char *line, ProvisioningCommand &out);

}  // namespace stopwatch
