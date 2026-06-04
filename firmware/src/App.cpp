// firmware/src/App.cpp
#include "App.h"

namespace stopwatch {

void App::begin() {
    view_ = ViewId::Overview;
    wantsRefresh_ = false;
    wantsSleep_ = false;
    detailIndex_ = -1;
    metric_ = UsageMetric::Cost;
    inCarouselSettings_ = false;
    settingRow_ = CarouselSettingRow::Transport;
}

bool App::handleEvent(ButtonEvent ev, CarouselSettings &settings) {
    if (ev == ButtonEvent::BothLong) {
        inCarouselSettings_ = !inCarouselSettings_;
        settingRow_ = CarouselSettingRow::Transport;
        settings.validate();
        return true;
    }

    if (inCarouselSettings_) {
        switch (ev) {
            case ButtonEvent::KeyBShort:
                settingRow_ = nextSettingRow(settingRow_);
                return true;
            case ButtonEvent::KeyAShort:
                settings.cycle(settingRow_);
                settings.validate();
                return true;
            case ButtonEvent::KeyALong:
                settings.resetDefaults();
                return true;
            case ButtonEvent::KeyBLong:
                wantsSleep_ = true;
                return false;
            case ButtonEvent::None:
            case ButtonEvent::BothLong:
                return false;
        }
        return false;
    }

    return handleEvent(ev);
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
            case ButtonEvent::BothLong:                        return false;
        }
        return false;
    }
    switch (ev) {
        case ButtonEvent::KeyBShort: view_ = nextView(view_); return true;
        case ButtonEvent::KeyAShort: view_ = prevView(view_); return true;
        case ButtonEvent::KeyALong:  wantsRefresh_ = true;    return false;
        case ButtonEvent::KeyBLong:  wantsSleep_   = true;    return false;
        case ButtonEvent::None:                                return false;
        case ButtonEvent::BothLong:                            return false;
    }
    return false;
}

void App::noteWakeFromSleep() {
    wantsRefresh_ = true;
    detailIndex_ = -1;   // wake to the list, never a stale detail index
    inCarouselSettings_ = false;
}

}  // namespace stopwatch
