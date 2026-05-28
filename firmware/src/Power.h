// firmware/src/Power.h
#pragma once
#include <cstdint>

namespace stopwatch {

class Power {
public:
    void begin();
    void noteActivity();      // call on every user input
    bool shouldSleep() const; // true after kIdleSleepMs of no activity
    void enterLightSleep();   // configures wake-on-button + sleeps; returns after wake

private:
    uint32_t lastActivityMs_ = 0;
    static constexpr uint32_t kIdleSleepMs = 15000;
};

}  // namespace stopwatch
