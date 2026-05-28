// firmware/src/main.cpp
#include <Arduino.h>
#include <M5Unified.h>

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setFont(&fonts::Font4);
    M5.Display.drawString("hello, stopwatch",
                          M5.Display.width() / 2,
                          M5.Display.height() / 2);
}

void loop() {
    M5.update();
    delay(50);
}
