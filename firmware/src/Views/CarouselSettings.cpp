#include "CarouselSettings.h"
#include "../Theme.h"
#include <cstddef>
#include <cstdio>

namespace stopwatch::views {

namespace {
constexpr uint32_t kSettingsBg = 0x05080C;
constexpr uint32_t kRowFill = 0x0C1218;
constexpr uint32_t kSelectedFill = 0x141D28;
constexpr int kRowX = 104;
constexpr int kRowW = 258;
constexpr int kRowH = 40;
constexpr int kRowR = 8;

const char *valueText(const CarouselSettings &settings, CarouselSettingRow row,
                      char *buf, size_t n) {
    switch (row) {
        case CarouselSettingRow::Transport:
            return CarouselSettings::transportLabel(settings.transportMode);
        case CarouselSettingRow::Upright:
            return settings.uprightEnabled ? "ON" : "OFF";
        case CarouselSettingRow::Autoplay:
            return settings.autoplayEnabled ? "ON" : "OFF";
        case CarouselSettingRow::Interval:
            snprintf(buf, n, "%us", (unsigned)settings.intervalSeconds);
            return buf;
        case CarouselSettingRow::Motion:
            return CarouselSettings::motionLabel(settings.motionMode);
        case CarouselSettingRow::Resume:
            snprintf(buf, n, "%us", (unsigned)settings.resumeSeconds);
            return buf;
    }
    return "?";
}

void drawGroup(M5Canvas &c, const char *label, int y) {
    c.setFont(theme::kFontMicro);
    c.setTextDatum(middle_left);
    c.setTextColor(theme::kTextMuted);
    c.drawString(label, kRowX + 4, y);
}

void drawRow(M5Canvas &c, const CarouselSettings &settings, CarouselSettingRow row,
             CarouselSettingRow selected, int y) {
    bool active = row == selected;
    c.fillRoundRect(kRowX, y - kRowH / 2, kRowW, kRowH, kRowR,
                    active ? kSelectedFill : kRowFill);
    if (active) {
        c.fillRoundRect(kRowX, y - kRowH / 2, 5, kRowH, 3, theme::kCodex);
    }

    c.setFont(theme::kFontBody);
    c.setTextDatum(middle_left);
    c.setTextColor(active ? theme::kTextPrimary : theme::kTextMuted);
    c.drawString(CarouselSettings::rowLabel(row), kRowX + 18, y);

    char buf[12];
    const char *value = valueText(settings, row, buf, sizeof(buf));
    c.setTextDatum(middle_right);
    c.setTextColor(active ? theme::kCodex : theme::kTextPrimary);
    c.drawString(value, kRowX + kRowW - 18, y);
}
}  // namespace

void drawCarouselSettings(Renderer &renderer, const CarouselSettings &settings,
                          CarouselSettingRow selected) {
    auto &c = renderer.canvas();
    renderer.clear(kSettingsBg);
    c.setTextDatum(middle_center);
    c.setFont(theme::kFontMicro);
    c.setTextColor(theme::kTextMuted);
    c.drawString("LOCAL", theme::kCenterX, 34);
    c.setFont(theme::kFontTitle);
    c.setTextColor(theme::kTextPrimary);
    c.drawString("SETTINGS", theme::kCenterX, 60);

    drawGroup(c, "CONNECTION", 88);
    drawRow(c, settings, CarouselSettingRow::Transport, selected, 112);

    drawGroup(c, "DISPLAY", 146);
    drawRow(c, settings, CarouselSettingRow::Upright, selected, 170);

    drawGroup(c, "CAROUSEL", 202);
    drawRow(c, settings, CarouselSettingRow::Autoplay, selected, 226);
    drawRow(c, settings, CarouselSettingRow::Interval, selected, 268);
    drawRow(c, settings, CarouselSettingRow::Motion, selected, 310);
    drawRow(c, settings, CarouselSettingRow::Resume, selected, 352);

    c.setTextDatum(middle_center);
    c.setFont(theme::kFontMicro);
    c.setTextColor(theme::kTextMuted);
    c.drawString("A CHANGE  B NEXT", theme::kCenterX, 424);
    c.drawString("A+B HOLD SAVE", theme::kCenterX, 444);
}

}  // namespace stopwatch::views
