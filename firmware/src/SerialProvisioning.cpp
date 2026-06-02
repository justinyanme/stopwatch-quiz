#include "SerialProvisioning.h"
#include "ProvisioningCommand.h"
#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#include <Preferences.h>
#endif

namespace stopwatch {

namespace {
constexpr const char *kNs = "swq-net";
constexpr const char *kSSID = "ssid";
constexpr const char *kPass = "pass";
constexpr const char *kBase = "base";
constexpr const char *kCFID = "cfid";
constexpr const char *kCFSecret = "cfsecret";
constexpr const char *kToken = "token";

#ifdef ARDUINO
Preferences prefs;
#endif
}  // namespace

void SerialProvisioning::begin() {
#ifdef ARDUINO
    open_ = prefs.begin(kNs, false);
#endif
}

bool SerialProvisioning::load(DeviceNetworkConfig &out) {
#ifdef ARDUINO
    if (!open_) return false;
    out = DeviceNetworkConfig{};
    prefs.getString(kSSID, out.wifiSSID, sizeof(out.wifiSSID));
    prefs.getString(kPass, out.wifiPassword, sizeof(out.wifiPassword));
    prefs.getString(kBase, out.apiBaseURL, sizeof(out.apiBaseURL));
    prefs.getString(kCFID, out.cfClientID, sizeof(out.cfClientID));
    prefs.getString(kCFSecret, out.cfClientSecret, sizeof(out.cfClientSecret));
    prefs.getString(kToken, out.apiToken, sizeof(out.apiToken));
    return true;
#else
    (void)out;
    return false;
#endif
}

void SerialProvisioning::clear() {
#ifdef ARDUINO
    (void)clearStore();
#endif
}

bool SerialProvisioning::saveString(const char *key, const char *value) {
#ifdef ARDUINO
    if (!open_) return false;
    return prefs.putString(key, value) > 0;
#else
    (void)key;
    (void)value;
    return false;
#endif
}

bool SerialProvisioning::clearStore() {
#ifdef ARDUINO
    return open_ && prefs.clear();
#else
    return false;
#endif
}

void SerialProvisioning::poll() {
#ifdef ARDUINO
    static char line[256];
    static size_t len = 0;
    while (Serial.available() > 0) {
        int b = Serial.read();
        if (b < 0) break;
        if (b == '\r') continue;
        if (b == '\n') {
            line[len] = '\0';
            applyLine(line);
            len = 0;
        } else if (len + 1 < sizeof(line)) {
            line[len++] = (char)b;
        }
    }
#endif
}

void SerialProvisioning::applyLine(const char *line) {
#ifdef ARDUINO
    ProvisioningCommand cmd;
    if (!parseProvisioningCommand(line, cmd)) {
        Serial.println("[provision] invalid command");
        return;
    }
    bool saved = false;
    switch (cmd.action) {
        case ProvisioningAction::SetWiFiSSID: saved = saveString(kSSID, cmd.value); break;
        case ProvisioningAction::SetWiFiPassword: saved = saveString(kPass, cmd.value); break;
        case ProvisioningAction::SetAPIBaseURL: saved = saveString(kBase, cmd.value); break;
        case ProvisioningAction::SetCFAccessClientID: saved = saveString(kCFID, cmd.value); break;
        case ProvisioningAction::SetCFAccessClientSecret: saved = saveString(kCFSecret, cmd.value); break;
        case ProvisioningAction::SetAPIToken: saved = saveString(kToken, cmd.value); break;
        case ProvisioningAction::ShowConfig: printConfig(); return;
        case ProvisioningAction::ClearConfig:
            Serial.println(clearStore() ? "[provision] cleared" : "[provision] clear failed");
            return;
    }
    Serial.println(saved ? "[provision] saved" : "[provision] save failed");
#else
    (void)line;
#endif
}

void SerialProvisioning::printConfig() {
#ifdef ARDUINO
    DeviceNetworkConfig cfg;
    load(cfg);
    Serial.printf("[provision] wifi_ssid=%s\n", cfg.wifiSSID[0] ? "set" : "missing");
    Serial.printf("[provision] wifi_password=%s\n", cfg.wifiPassword[0] ? "set" : "missing");
    Serial.printf("[provision] api_base_url=%s\n", cfg.apiBaseURL[0] ? cfg.apiBaseURL : "missing");
    Serial.printf("[provision] cf_client_id=%s len=%u\n",
                  cfg.cfClientID[0] ? "set" : "missing",
                  (unsigned)std::strlen(cfg.cfClientID));
    Serial.printf("[provision] cf_client_secret=%s len=%u\n",
                  cfg.cfClientSecret[0] ? "set" : "missing",
                  (unsigned)std::strlen(cfg.cfClientSecret));
    Serial.printf("[provision] api_token=%s len=%u\n",
                  cfg.apiToken[0] ? "set" : "missing",
                  (unsigned)std::strlen(cfg.apiToken));
#endif
}

}  // namespace stopwatch
