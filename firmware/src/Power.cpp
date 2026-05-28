// firmware/src/Power.cpp
#include "Power.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <M5Unified.h>

namespace stopwatch {

// KEYA / KEYB GPIO numbers verified against M5Unified's per-board button-poll code
// (M5Unified.cpp board_M5StopWatch branch): KEYA is GPIO2, KEYB is GPIO1, both
// active-low. ext1 wake uses ANY_LOW which matches.
namespace { constexpr gpio_num_t kPinKeyA = GPIO_NUM_2;
              constexpr gpio_num_t kPinKeyB = GPIO_NUM_1;

void waitForButtonsReleased() {
    M5.update();
    while (M5.BtnA.isPressed() || M5.BtnB.isPressed()) {
        delay(20);
        M5.update();
    }
}
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
    waitForButtonsReleased();
    M5.Display.sleep();
    uint64_t mask = (1ULL << kPinKeyA) | (1ULL << kPinKeyB);
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_light_sleep_start();
    // Woke up.
    M5.Display.wakeup();
    noteActivity();
}

}  // namespace stopwatch
