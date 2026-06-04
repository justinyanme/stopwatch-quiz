#include "NetworkClient.h"
#include "Protocol.h"
#include "SerialProvisioning.h"
#include <cstdio>
#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#endif

namespace stopwatch {

namespace {
SerialProvisioning netStore;
constexpr uint32_t kRefreshPollInitialDelayMs = 150;
constexpr uint32_t kRefreshPollIntervalMs = 350;
constexpr uint32_t kRefreshPollTimeoutMs = 6000;

#ifndef ARDUINO
const NetworkClient::NativeTestHooks *nativeTestHooks = nullptr;
#endif

bool appendURL(char *out, size_t n, const char *base, const char *path) {
    if (!base || !path || !out || n == 0) return false;
    size_t blen = std::strlen(base);
    bool slash = blen > 0 && base[blen - 1] == '/';
    int written = std::snprintf(out, n, "%s%s%s", base, slash ? "" : "/", path[0] == '/' ? path + 1 : path);
    return written > 0 && (size_t)written < n;
}

uint32_t readU32(const uint8_t *b) {
    return (uint32_t)b[0] |
           ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
}

uint32_t capturedAt(const uint8_t *bytes, size_t len) {
    return len >= 8 ? readU32(bytes + 4) : 0;
}

void refreshPollDelay(uint32_t ms) {
#ifdef ARDUINO
    delay(ms);
#else
    if (nativeTestHooks && nativeTestHooks->delayMs) {
        nativeTestHooks->delayMs(ms, nativeTestHooks->context);
    }
#endif
}

#ifdef ARDUINO
// Sink for HTTPClient::writeToStream that captures the response body into a
// fixed buffer. writeToStream de-chunks Transfer-Encoding: chunked responses for
// us, so this works whether or not Content-Length is present — Cloudflare Tunnel
// commonly re-frames responses as chunked (getSize() == -1), which a raw
// read-by-Content-Length would reject or read with chunk framing intact.
class BufferSink : public Stream {
public:
    BufferSink(uint8_t *buf, size_t cap) : buf_(buf), cap_(cap) {}
    size_t length() const { return len_; }
    bool overflowed() const { return overflow_; }

    size_t write(uint8_t b) override {
        if (len_ >= cap_) { overflow_ = true; return 0; }
        buf_[len_++] = b;
        return 1;
    }
    size_t write(const uint8_t *data, size_t size) override {
        if (size > cap_ - len_) { overflow_ = true; return 0; }  // short write -> writeToStream aborts
        std::memcpy(buf_ + len_, data, size);
        len_ += size;
        return size;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }

private:
    uint8_t *buf_;
    size_t cap_;
    size_t len_ = 0;
    bool overflow_ = false;
};
#endif
}  // namespace

#ifndef ARDUINO
void NetworkClient::setNativeTestHooks(const NativeTestHooks *hooks) {
    nativeTestHooks = hooks;
}
#endif

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
#ifndef ARDUINO
    if (nativeTestHooks && nativeTestHooks->get) {
        return nativeTestHooks->get(path, outBytes, bufSize, outLen, nativeTestHooks->context);
    }
#endif
    DeviceNetworkConfig cfg;
    netStore.load(cfg);
    if (!cfg.apiConfigured()) return FetchResult::APIMissing;
    FetchResult wifi = ensureWiFi(cfg);
    if (wifi != FetchResult::Ok) return wifi;

#ifdef ARDUINO
    char url[256];
    if (!appendURL(url, sizeof(url), cfg.apiBaseURL, path)) return FetchResult::APIMissing;
    // The bridge is reached over HTTPS through Cloudflare Tunnel, so an https URL
    // needs a TLS client. setInsecure() skips cert validation — the origin Bearer
    // token + Cloudflare Access provide auth; pin the Cloudflare CA here for a
    // stricter posture. http:// (e.g. a direct loopback test) uses a plain client.
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    const bool isHttps = std::strncmp(url, "https://", 8) == 0;
    if (isHttps) secureClient.setInsecure();
    HTTPClient http;
    const bool began = isHttps ? http.begin(secureClient, url) : http.begin(plainClient, url);
    if (!began) return FetchResult::RequestFailed;
    http.setConnectTimeout(8000);
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
    // A known Content-Length larger than the buffer is an oversized/bad payload.
    int len = http.getSize();
    if (len > 0 && (size_t)len > bufSize) {
        http.end();
        return FetchResult::BadPayload;
    }
    // writeToStream handles both identity and chunked transfer-encoding; reading
    // by Content-Length (getSize()) breaks when the tunnel sends chunked (len == -1).
    BufferSink sink(outBytes, bufSize);
    int written = http.writeToStream(&sink);
    http.end();
    if (sink.overflowed() || written < 0 || sink.length() == 0) {
        return FetchResult::BadPayload;
    }
    outLen = sink.length();
    return FetchResult::Ok;
#else
    (void)path;
    (void)outBytes;
    (void)bufSize;
    return FetchResult::RequestFailed;
#endif
}

NetworkClient::FetchResult NetworkClient::postPath(const char *path) {
#ifndef ARDUINO
    if (nativeTestHooks && nativeTestHooks->post) {
        return nativeTestHooks->post(path, nativeTestHooks->context);
    }
#endif
    DeviceNetworkConfig cfg;
    netStore.load(cfg);
    if (!cfg.apiConfigured()) return FetchResult::APIMissing;
    FetchResult wifi = ensureWiFi(cfg);
    if (wifi != FetchResult::Ok) return wifi;

#ifdef ARDUINO
    char url[256];
    if (!appendURL(url, sizeof(url), cfg.apiBaseURL, path)) return FetchResult::APIMissing;
    // The bridge is reached over HTTPS through Cloudflare Tunnel, so an https URL
    // needs a TLS client. setInsecure() skips cert validation — the origin Bearer
    // token + Cloudflare Access provide auth; pin the Cloudflare CA here for a
    // stricter posture. http:// (e.g. a direct loopback test) uses a plain client.
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    const bool isHttps = std::strncmp(url, "https://", 8) == 0;
    if (isHttps) secureClient.setInsecure();
    HTTPClient http;
    const bool began = isHttps ? http.begin(secureClient, url) : http.begin(plainClient, url);
    if (!began) return FetchResult::RequestFailed;
    http.setConnectTimeout(8000);
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

NetworkClient::FetchResult NetworkClient::refreshAndFetch(
    uint8_t scope,
    Fetcher fetcher,
    uint8_t *outBytes,
    size_t bufSize,
    size_t &outLen)
{
    // The bridge accepts refreshes asynchronously; use capturedAt as the common
    // frame-level signal that the repository has published a newer payload.
    size_t baselineLen = 0;
    FetchResult baseline = (this->*fetcher)(outBytes, bufSize, baselineLen);
    const bool hasBaseline = baseline == FetchResult::Ok;
    const uint32_t baselineCapturedAt = hasBaseline ? capturedAt(outBytes, baselineLen) : 0;

    FetchResult rc = refresh(scope);
    if (rc != FetchResult::Ok) {
        outLen = 0;
        return rc;
    }

    uint32_t waited = 0;
    bool hasOkPayload = false;
    FetchResult lastResult = FetchResult::RequestFailed;

    while (waited <= kRefreshPollTimeoutMs) {
        const uint32_t delayMs = waited == 0 ? kRefreshPollInitialDelayMs : kRefreshPollIntervalMs;
        refreshPollDelay(delayMs);
        waited += delayMs;

        rc = (this->*fetcher)(outBytes, bufSize, outLen);
        lastResult = rc;
        if (rc != FetchResult::Ok) continue;

        hasOkPayload = true;
        const uint32_t currentCapturedAt = capturedAt(outBytes, outLen);
        if (!hasBaseline || currentCapturedAt != baselineCapturedAt) {
            return FetchResult::Ok;
        }
    }

    if (hasOkPayload && baselineCapturedAt != 0) {
        return FetchResult::Ok;
    }
    outLen = 0;
    return lastResult == FetchResult::Ok ? FetchResult::RequestFailed : lastResult;
}

NetworkClient::FetchResult NetworkClient::refreshAndFetchSnapshot(
    uint8_t scope,
    uint8_t *outBytes,
    size_t bufSize,
    size_t &outLen)
{
    return refreshAndFetch(scope, &NetworkClient::fetchSnapshot, outBytes, bufSize, outLen);
}

NetworkClient::FetchResult NetworkClient::refreshAndFetchCost(
    uint8_t *outBytes,
    size_t bufSize,
    size_t &outLen)
{
    return refreshAndFetch(kTriggerScopeCost, &NetworkClient::fetchCost, outBytes, bufSize, outLen);
}

NetworkClient::FetchResult NetworkClient::refreshAndFetchBalances(
    uint8_t *outBytes,
    size_t bufSize,
    size_t &outLen)
{
    return refreshAndFetch(kTriggerScopeBalances, &NetworkClient::fetchBalances, outBytes, bufSize, outLen);
}

NetworkClient::FetchResult NetworkClient::refreshAndFetchUsage(
    uint8_t *outBytes,
    size_t bufSize,
    size_t &outLen)
{
    return refreshAndFetch(kTriggerScopeUsage, &NetworkClient::fetchUsage, outBytes, bufSize, outLen);
}

}  // namespace stopwatch
