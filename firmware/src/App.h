// firmware/src/App.h
#pragma once
#include "Buttons.h"
#include "Protocol.h"
#include "SnapshotCodec.h"

namespace stopwatch {

enum class ViewId : uint8_t {
    Overview = 0, TotalSpend = 1,
    Codex = 2, CodexCost = 3,
    Claude = 4, ClaudeCost = 5,
    Gemini = 6,
};

enum class LinkStatus : uint8_t {
    Connected,    // last fetch returned Ok
    NoBridge,     // BleClient::FetchResult::NoPeripheral
    LinkError,    // ConnectFailed or ReadFailed (after retry)
};

class App {
public:
    void begin();
    /// Drive one event into the state machine; returns true if the view changed.
    bool handleEvent(ButtonEvent ev);
    ViewId currentView() const { return view_; }
    bool wantsRefresh() const { return wantsRefresh_; }
    void clearRefreshRequest() { wantsRefresh_ = false; }
    void noteWakeFromSleep();
    bool wantsImmediateSleep() const { return wantsSleep_; }
    void clearSleepRequest() { wantsSleep_ = false; }
    LinkStatus linkStatus() const { return link_; }
    void setLinkStatus(LinkStatus s) { link_ = s; }

private:
    ViewId view_ = ViewId::Overview;
    bool wantsRefresh_ = false;
    bool wantsSleep_   = false;
    LinkStatus link_ = LinkStatus::NoBridge;
};

inline constexpr ViewId nextView(ViewId v) {
    switch (v) {
        case ViewId::Overview:   return ViewId::TotalSpend;
        case ViewId::TotalSpend: return ViewId::Codex;
        case ViewId::Codex:      return ViewId::CodexCost;
        case ViewId::CodexCost:  return ViewId::Claude;
        case ViewId::Claude:     return ViewId::ClaudeCost;
        case ViewId::ClaudeCost: return ViewId::Gemini;
        case ViewId::Gemini:     return ViewId::Overview;
    }
    return ViewId::Overview;
}

inline constexpr ViewId prevView(ViewId v) {
    switch (v) {
        case ViewId::Overview:   return ViewId::Gemini;
        case ViewId::TotalSpend: return ViewId::Overview;
        case ViewId::Codex:      return ViewId::TotalSpend;
        case ViewId::CodexCost:  return ViewId::Codex;
        case ViewId::Claude:     return ViewId::CodexCost;
        case ViewId::ClaudeCost: return ViewId::Claude;
        case ViewId::Gemini:     return ViewId::ClaudeCost;
    }
    return ViewId::Overview;
}

constexpr bool isSpendView(ViewId v) {
    return v == ViewId::TotalSpend || v == ViewId::CodexCost || v == ViewId::ClaudeCost;
}

}  // namespace stopwatch
