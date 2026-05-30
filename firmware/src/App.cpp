// firmware/src/App.cpp
#include "App.h"

namespace stopwatch {

void App::begin() {
    view_ = ViewId::Overview;
    wantsRefresh_ = false;
    wantsSleep_ = false;
    detailIndex_ = -1;
    metric_ = UsageMetric::Cost;
}

bool App::handleEvent(ButtonEvent ev) {
    // Inside a balance detail: A backs out, B toggles the chart metric; neither
    // moves the carousel. Long-presses keep their global meaning.
    if (detailIndex_ >= 0) {
        switch (ev) {
            case ButtonEvent::KeyAShort: detailIndex_ = -1; return true;          // back to list
            case ButtonEvent::KeyBShort:
                metric_ = (metric_ == UsageMetric::Cost) ? UsageMetric::Tokens : UsageMetric::Cost;
                return true;
            case ButtonEvent::KeyALong:  wantsRefresh_ = true; return false;
            case ButtonEvent::KeyBLong:  wantsSleep_   = true; return false;
            case ButtonEvent::None:                            return false;
        }
        return false;
    }
    switch (ev) {
        case ButtonEvent::KeyBShort: view_ = nextView(view_); return true;
        case ButtonEvent::KeyAShort: view_ = prevView(view_); return true;
        case ButtonEvent::KeyALong:  wantsRefresh_ = true;    return false;
        case ButtonEvent::KeyBLong:  wantsSleep_   = true;    return false;
        case ButtonEvent::None:                                return false;
    }
    return false;
}

void App::noteWakeFromSleep() {
    wantsRefresh_ = true;
}

}  // namespace stopwatch
