#include "ProvisioningCommand.h"
#include <cstring>

namespace stopwatch {
namespace {

bool startsWith(const char *line, const char *prefix) {
    return std::strncmp(line, prefix, std::strlen(prefix)) == 0;
}

bool copyValue(const char *value, ProvisioningCommand &out) {
    if (!value || value[0] == '\0') return false;
    std::strncpy(out.value, value, sizeof(out.value) - 1);
    out.value[sizeof(out.value) - 1] = '\0';
    return true;
}

bool setWithValue(const char *line, const char *prefix,
                  ProvisioningAction action, ProvisioningCommand &out) {
    if (!startsWith(line, prefix)) return false;
    out.action = action;
    return copyValue(line + std::strlen(prefix), out);
}

}  // namespace

bool parseProvisioningCommand(const char *line, ProvisioningCommand &out) {
    if (!line || line[0] == '\0') return false;
    out.value[0] = '\0';

    if (setWithValue(line, "wifi ssid ", ProvisioningAction::SetWiFiSSID, out)) return true;
    if (setWithValue(line, "wifi password ", ProvisioningAction::SetWiFiPassword, out)) return true;
    if (setWithValue(line, "api base-url ", ProvisioningAction::SetAPIBaseURL, out)) return true;
    if (setWithValue(line, "api cf-client-id ", ProvisioningAction::SetCFAccessClientID, out)) return true;
    if (setWithValue(line, "api cf-client-secret ", ProvisioningAction::SetCFAccessClientSecret, out)) return true;
    if (setWithValue(line, "api token ", ProvisioningAction::SetAPIToken, out)) return true;

    if (std::strcmp(line, "config show") == 0) {
        out.action = ProvisioningAction::ShowConfig;
        return true;
    }
    if (std::strcmp(line, "config clear") == 0) {
        out.action = ProvisioningAction::ClearConfig;
        return true;
    }
    return false;
}

}  // namespace stopwatch
