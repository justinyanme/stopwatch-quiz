// firmware/src/App.h
#pragma once
#include "Buttons.h"
#include "CarouselSettings.h"
#include "NetworkClient.h"
#include "Protocol.h"
#include "SnapshotCodec.h"

namespace stopwatch {

enum class ViewId : uint8_t {
    Overview = 0, TotalSpend = 1,
    Codex = 2, CodexCost = 3,
    Claude = 4, ClaudeCost = 5,
    Gemini = 6, Balances = 7,
};

enum class LinkStatus : uint8_t {
    Connected,    // last fetch returned Ok
    NoBridge,     // BleClient::FetchResult::NoPeripheral
    LinkError,    // ConnectFailed or ReadFailed (after retry)
    WiFiMissing,
    APIMissing,
    WiFiOffline,
    APIAuth,
    APIError,
};

enum class UsageMetric : uint8_t { Cost = 0, Tokens = 1 };

class App {
public:
    void begin();
    /// Drive one event into the state machine; returns true if the view changed.
    bool handleEvent(ButtonEvent ev);
    bool handleEvent(ButtonEvent ev, CarouselSettings &settings);
    ViewId currentView() const { return view_; }
    bool wantsRefresh() const { return wantsRefresh_; }
    void clearRefreshRequest() { wantsRefresh_ = false; }
    void noteWakeFromSleep();
    bool wantsImmediateSleep() const { return wantsSleep_; }
    void clearSleepRequest() { wantsSleep_ = false; }
    LinkStatus linkStatus() const { return link_; }
    void setLinkStatus(LinkStatus s) { link_ = s; }

    bool inBalanceDetail() const { return detailIndex_ >= 0; }
    int  balanceDetailIndex() const { return detailIndex_; }
    void enterBalanceDetail(int recordIndex) {
        if (inCarouselSettings_) return;
        detailIndex_ = recordIndex;
        metric_ = UsageMetric::Cost;
    }
    void exitBalanceDetail() { detailIndex_ = -1; }
    UsageMetric usageMetric() const { return metric_; }
    bool inCarouselSettings() const { return inCarouselSettings_; }
    CarouselSettingRow carouselSettingRow() const { return settingRow_; }
    void exitCarouselSettings() { inCarouselSettings_ = false; }

private:
    ViewId view_ = ViewId::Overview;
    bool wantsRefresh_ = false;
    bool wantsSleep_   = false;
    LinkStatus link_ = LinkStatus::NoBridge;
    int detailIndex_ = -1;          // -1 = list; >=0 = showing that record's detail
    UsageMetric metric_ = UsageMetric::Cost;
    bool inCarouselSettings_ = false;
    CarouselSettingRow settingRow_ = CarouselSettingRow::Transport;
};

inline constexpr ViewId nextView(ViewId v) {
    switch (v) {
        case ViewId::Overview:   return ViewId::TotalSpend;
        case ViewId::TotalSpend: return ViewId::Codex;
        case ViewId::Codex:      return ViewId::CodexCost;
        case ViewId::CodexCost:  return ViewId::Claude;
        case ViewId::Claude:     return ViewId::ClaudeCost;
        case ViewId::ClaudeCost: return ViewId::Gemini;
        case ViewId::Gemini:     return ViewId::Balances;
        case ViewId::Balances:   return ViewId::Overview;
    }
    return ViewId::Overview;
}

inline constexpr ViewId prevView(ViewId v) {
    switch (v) {
        case ViewId::Overview:   return ViewId::Balances;
        case ViewId::TotalSpend: return ViewId::Overview;
        case ViewId::Codex:      return ViewId::TotalSpend;
        case ViewId::CodexCost:  return ViewId::Codex;
        case ViewId::Claude:     return ViewId::CodexCost;
        case ViewId::ClaudeCost: return ViewId::Claude;
        case ViewId::Gemini:     return ViewId::ClaudeCost;
        case ViewId::Balances:   return ViewId::Gemini;
    }
    return ViewId::Overview;
}

constexpr bool isSpendView(ViewId v) {
    return v == ViewId::TotalSpend || v == ViewId::CodexCost || v == ViewId::ClaudeCost;
}

constexpr bool isBalanceView(ViewId v) { return v == ViewId::Balances; }

inline LinkStatus linkStatusForFetchResult(TransportMode mode, NetworkClient::FetchResult result) {
    switch (result) {
        case NetworkClient::FetchResult::Ok:
            return LinkStatus::Connected;
        case NetworkClient::FetchResult::WiFiMissing:
            return mode == TransportMode::WiFi ? LinkStatus::WiFiMissing : LinkStatus::LinkError;
        case NetworkClient::FetchResult::APIMissing:
            return mode == TransportMode::WiFi ? LinkStatus::APIMissing : LinkStatus::LinkError;
        case NetworkClient::FetchResult::WiFiOffline:
            return mode == TransportMode::WiFi ? LinkStatus::WiFiOffline : LinkStatus::NoBridge;
        case NetworkClient::FetchResult::AuthFailed:
            return mode == TransportMode::WiFi ? LinkStatus::APIAuth : LinkStatus::LinkError;
        case NetworkClient::FetchResult::RequestFailed:
        case NetworkClient::FetchResult::BadPayload:
            return mode == TransportMode::WiFi ? LinkStatus::APIError : LinkStatus::LinkError;
    }
    return LinkStatus::LinkError;
}

}  // namespace stopwatch
