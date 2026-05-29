// firmware/src/App.cpp
#include "App.h"

namespace stopwatch {

void App::begin() {
    view_ = ViewId::Overview;
    wantsRefresh_ = false;
    wantsSleep_ = false;
}

bool App::handleEvent(ButtonEvent ev) {
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
