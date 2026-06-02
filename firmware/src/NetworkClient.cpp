#include "NetworkClient.h"
#include "SerialProvisioning.h"
#include <cstdio>
#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#endif

namespace stopwatch {

namespace {
SerialProvisioning netStore;

bool appendURL(char *out, size_t n, const char *base, const char *path) {
    if (!base || !path || !out || n == 0) return false;
    size_t blen = std::strlen(base);
    bool slash = blen > 0 && base[blen - 1] == '/';
    int written = std::snprintf(out, n, "%s%s%s", base, slash ? "" : "/", path[0] == '/' ? path + 1 : path);
    return written > 0 && (size_t)written < n;
}
}  // namespace

void NetworkClient::begin() {
    netStore.begin();
}

NetworkClient::FetchResult NetworkClient::ensureWiFi(const DeviceNetworkConfig &cfg) {
#ifdef ARDUINO
    if (!cfg.wifiConfigured()) return FetchResult::WiFiMissing;
    if (WiFi.status() == WL_CONNECTED) return FetchResult::Ok;
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSSID, cfg.wifiPassword);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && (uint32_t)(millis() - started) < 8000) {
        delay(100);
    }
    return WiFi.status() == WL_CONNECTED ? FetchResult::Ok : FetchResult::WiFiOffline;
#else
    (void)cfg;
    return FetchResult::WiFiOffline;
#endif
}

NetworkClient::FetchResult NetworkClient::fetchPath(
    const char *path,
    uint8_t *outBytes,
    size_t bufSize,
    size_t &outLen)
{
    outLen = 0;
    DeviceNetworkConfig cfg;
    netStore.load(cfg);
    if (!cfg.apiConfigured()) return FetchResult::APIMissing;
    FetchResult wifi = ensureWiFi(cfg);
    if (wifi != FetchResult::Ok) return wifi;

#ifdef ARDUINO
    char url[256];
    if (!appendURL(url, sizeof(url), cfg.apiBaseURL, path)) return FetchResult::APIMissing;
    HTTPClient http;
    if (!http.begin(url)) return FetchResult::RequestFailed;
    http.addHeader("CF-Access-Client-Id", cfg.cfClientID);
    http.addHeader("CF-Access-Client-Secret", cfg.cfClientSecret);
    char auth[224];
    std::snprintf(auth, sizeof(auth), "Bearer %s", cfg.apiToken);
    http.addHeader("Authorization", auth);
    int code = http.GET();
    if (code == 401 || code == 403) {
        http.end();
        return FetchResult::AuthFailed;
    }
    if (code != 200) {
        http.end();
        return FetchResult::RequestFailed;
    }
    int len = http.getSize();
    if (len <= 0 || (size_t)len > bufSize) {
        http.end();
        return FetchResult::BadPayload;
    }
    WiFiClient *stream = http.getStreamPtr();
    size_t read = stream->readBytes(outBytes, len);
    http.end();
    if (read != (size_t)len) return FetchResult::BadPayload;
    outLen = read;
    return FetchResult::Ok;
#else
    (void)path;
    (void)outBytes;
    (void)bufSize;
    return FetchResult::RequestFailed;
#endif
}

NetworkClient::FetchResult NetworkClient::postPath(const char *path) {
    DeviceNetworkConfig cfg;
    netStore.load(cfg);
    if (!cfg.apiConfigured()) return FetchResult::APIMissing;
    FetchResult wifi = ensureWiFi(cfg);
    if (wifi != FetchResult::Ok) return wifi;

#ifdef ARDUINO
    char url[256];
    if (!appendURL(url, sizeof(url), cfg.apiBaseURL, path)) return FetchResult::APIMissing;
    HTTPClient http;
    if (!http.begin(url)) return FetchResult::RequestFailed;
    http.addHeader("CF-Access-Client-Id", cfg.cfClientID);
    http.addHeader("CF-Access-Client-Secret", cfg.cfClientSecret);
    char auth[224];
    std::snprintf(auth, sizeof(auth), "Bearer %s", cfg.apiToken);
    http.addHeader("Authorization", auth);
    int code = http.POST("");
    http.end();
    if (code == 401 || code == 403) return FetchResult::AuthFailed;
    if (code != 200 && code != 202) return FetchResult::RequestFailed;
    return FetchResult::Ok;
#else
    (void)path;
    return FetchResult::RequestFailed;
#endif
}

NetworkClient::FetchResult NetworkClient::fetchSnapshot(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchPath("/v1/snapshot", outBytes, bufSize, outLen);
}

NetworkClient::FetchResult NetworkClient::fetchCost(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchPath("/v1/cost", outBytes, bufSize, outLen);
}

NetworkClient::FetchResult NetworkClient::fetchBalances(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchPath("/v1/balances", outBytes, bufSize, outLen);
}

NetworkClient::FetchResult NetworkClient::fetchUsage(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchPath("/v1/balance-usage", outBytes, bufSize, outLen);
}

NetworkClient::FetchResult NetworkClient::refresh(uint8_t scope) {
    char path[32];
    std::snprintf(path, sizeof(path), "/v1/refresh?scope=%u", (unsigned)scope);
    return postPath(path);
}

}  // namespace stopwatch
