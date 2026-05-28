// firmware/src/App.h
#pragma once
#include "Buttons.h"
#include "Protocol.h"
#include "SnapshotCodec.h"

namespace stopwatch {

enum class ViewId : uint8_t { Overview = 0, Codex = 1, Claude = 2, Gemini = 3 };

class App {
public:
    void begin();
    /// Drive one event into the state machine; returns true if the view changed.
    bool handleEvent(ButtonEvent ev);
    ViewId currentView() const { return view_; }
    bool wantsRefresh() const { return wantsRefresh_; }
    void clearRefreshRequest() { wantsRefresh_ = false; }
    bool wantsImmediateSleep() const { return wantsSleep_; }
    void clearSleepRequest() { wantsSleep_ = false; }

private:
    ViewId view_ = ViewId::Overview;
    bool wantsRefresh_ = false;
    bool wantsSleep_   = false;
};

constexpr ViewId nextView(ViewId v);
constexpr ViewId prevView(ViewId v);

}  // namespace stopwatch
