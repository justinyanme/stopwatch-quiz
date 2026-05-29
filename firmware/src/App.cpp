// firmware/src/App.cpp
#include "App.h"

namespace stopwatch {

constexpr ViewId nextView(ViewId v) {
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

constexpr ViewId prevView(ViewId v) {
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
