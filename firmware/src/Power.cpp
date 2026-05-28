// firmware/src/Power.cpp
#include "Power.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <M5Unified.h>

namespace stopwatch {

// IMPORTANT: Confirm the actual GPIOs for KEYA / KEYB on the M5Stack StopWatch
// before flashing in Phase C. M5Unified's pin map abstracts BtnA/BtnB but ext1
// wake needs raw GPIO numbers. These are PLACEHOLDERS verified against the
// M5Stack StopWatch schematic during integration.
namespace { constexpr gpio_num_t kPinKeyA = GPIO_NUM_0;
              constexpr gpio_num_t kPinKeyB = GPIO_NUM_46;
}

void Power::begin() {
    noteActivity();
    M5.Display.setBrightness(160);
}

void Power::noteActivity() {
    lastActivityMs_ = millis();
}

bool Power::shouldSleep() const {
    return (millis() - lastActivityMs_) >= kIdleSleepMs;
}

void Power::enterLightSleep() {
    M5.Display.sleep();
    uint64_t mask = (1ULL << kPinKeyA) | (1ULL << kPinKeyB);
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_light_sleep_start();
    // Woke up.
    M5.Display.wakeup();
    noteActivity();
}

}  // namespace stopwatch
