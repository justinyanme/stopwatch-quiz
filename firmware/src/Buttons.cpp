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
bool sBothWasPressed = false;
uint32_t sBothPressedAt = 0;
bool sBothLongFired = false;

void beginPress(State &s, uint32_t now) {
    s.wasPressed = true;
    s.pressedAt = now;
    s.longFired = false;
}

void cancelSingleLongs() {
    sA.longFired = true;
    sB.longFired = true;
}

ButtonEvent step(State &s, bool pressed, ButtonEvent shortEv, ButtonEvent longEv, uint32_t now) {
    if (pressed && !s.wasPressed) {
        beginPress(s, now);
    } else if (pressed && s.wasPressed && !s.longFired && (now - s.pressedAt) >= kLongMs) {
        s.longFired = true;
        return longEv;
    } else if (!pressed && s.wasPressed) {
        bool wasLongEnough = (now - s.pressedAt) >= kLongMs;
        s.wasPressed = false;
        if (!s.longFired && !wasLongEnough) return shortEv;
    }
    return ButtonEvent::None;
}
}  // namespace

ButtonEvent pollButtons() {
    uint32_t now = millis();
    bool a = M5.BtnA.isPressed();
    bool b = M5.BtnB.isPressed();

    if (a && !sA.wasPressed) beginPress(sA, now);
    if (b && !sB.wasPressed) beginPress(sB, now);

    if (a && b) {
        if (!sBothWasPressed) {
            bool singleAlreadyDue = (now - sA.pressedAt) >= kLongMs ||
                                    (now - sB.pressedAt) >= kLongMs;
            if (!singleAlreadyDue && !sA.longFired && !sB.longFired) {
                sBothWasPressed = true;
                sBothPressedAt = now;
                sBothLongFired = false;
            }
        }
        if (sBothWasPressed) {
            if (!sBothLongFired && (now - sBothPressedAt) >= kLongMs) {
                sBothLongFired = true;
                cancelSingleLongs();
                return ButtonEvent::BothLong;
            }
            return ButtonEvent::None;
        }
    } else {
        sBothWasPressed = false;
    }

    auto evA = step(sA, a, ButtonEvent::KeyAShort, ButtonEvent::KeyALong, now);
    if (evA != ButtonEvent::None) return evA;
    return step(sB, b, ButtonEvent::KeyBShort, ButtonEvent::KeyBLong, now);
}

}  // namespace stopwatch
