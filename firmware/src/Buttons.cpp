// firmware/src/Buttons.cpp
#include "Buttons.h"
#include <M5Unified.h>

namespace stopwatch {

namespace {
constexpr uint32_t kLongMs = 800;

struct State {
    bool wasPressed = false;
    uint32_t pressedAt = 0;
    bool longFired = false;
};
State sA, sB;

ButtonEvent step(State &s, bool pressed, ButtonEvent shortEv, ButtonEvent longEv) {
    uint32_t now = millis();
    if (pressed && !s.wasPressed) {
        s.wasPressed = true;
        s.pressedAt = now;
        s.longFired = false;
    } else if (pressed && s.wasPressed && !s.longFired && (now - s.pressedAt) >= kLongMs) {
        s.longFired = true;
        return longEv;
    } else if (!pressed && s.wasPressed) {
        s.wasPressed = false;
        if (!s.longFired) return shortEv;
    }
    return ButtonEvent::None;
}
}  // namespace

ButtonEvent pollButtons() {
    M5.update();
    auto evA = step(sA, M5.BtnA.isPressed(), ButtonEvent::KeyAShort, ButtonEvent::KeyALong);
    if (evA != ButtonEvent::None) return evA;
    return step(sB, M5.BtnB.isPressed(), ButtonEvent::KeyBShort, ButtonEvent::KeyBLong);
}

}  // namespace stopwatch
