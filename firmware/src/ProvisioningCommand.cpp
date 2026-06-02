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
    if (!copyValue(line + std::strlen(prefix), out)) return false;
    out.action = action;
    return true;
}

}  // namespace

bool parseProvisioningCommand(const char *line, ProvisioningCommand &out) {
    if (!line || line[0] == '\0') return false;

    ProvisioningCommand candidate;
    if (setWithValue(line, "wifi ssid ", ProvisioningAction::SetWiFiSSID, candidate)) {
        out = candidate;
        return true;
    }
    if (setWithValue(line, "wifi password ", ProvisioningAction::SetWiFiPassword, candidate)) {
        out = candidate;
        return true;
    }
    if (setWithValue(line, "api base-url ", ProvisioningAction::SetAPIBaseURL, candidate)) {
        out = candidate;
        return true;
    }
    if (setWithValue(line, "api cf-client-id ", ProvisioningAction::SetCFAccessClientID, candidate)) {
        out = candidate;
        return true;
    }
    if (setWithValue(line, "api cf-client-secret ", ProvisioningAction::SetCFAccessClientSecret, candidate)) {
        out = candidate;
        return true;
    }
    if (setWithValue(line, "api token ", ProvisioningAction::SetAPIToken, candidate)) {
        out = candidate;
        return true;
    }

    if (std::strcmp(line, "config show") == 0) {
        candidate.action = ProvisioningAction::ShowConfig;
        out = candidate;
        return true;
    }
    if (std::strcmp(line, "config clear") == 0) {
        candidate.action = ProvisioningAction::ClearConfig;
        out = candidate;
        return true;
    }
    return false;
}

}  // namespace stopwatch
