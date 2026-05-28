// firmware/src/Views/Overview.cpp
#include "Overview.h"
#include "../App.h"
#include "../IconLookup.h"
#include "../Theme.h"
#include <cstdio>

namespace stopwatch::views {

namespace {
// Returns the provider in `snap` with the highest sessionPct, or nullptr if none.
const ProviderSlot *worstOff(const Snapshot &snap) {
    const ProviderSlot *best = nullptr;
    int bestPct = -1;
    for (uint8_t i = 0; i < snap.providerCount; ++i) {
        const auto &p = snap.providers[i];
        int pct = p.sessionPct.value_or(0);
        if (pct > bestPct) { bestPct = pct; best = &p; }
    }
    return best;
}

// Find a specific provider by ID.
const ProviderSlot *findProvider(const Snapshot &snap, ProviderID id) {
    for (uint8_t i = 0; i < snap.providerCount; ++i) {
        if (snap.providers[i].id == id) return &snap.providers[i];
    }
    return nullptr;
}

float fractionOf(const ProviderSlot *p) {
    if (!p || !p->sessionPct.has_value()) return 0.0f;
    return (float)p->sessionPct.value() / 100.0f;
}

struct Pill { const char *label; uint32_t color; };
Pill pillFor(LinkStatus link, const Snapshot &snap) {
    if (link == LinkStatus::NoBridge)            return { "no bridge", theme::kPillInfo };
    if (link == LinkStatus::LinkError)           return { "link error", theme::kPillError };
    if (snap.isProviderMissing())                return { "no source", theme::kPillInfo };
    if (snap.isStale() || snap.isBridgeError())  return { "stale", theme::kPillStale };
    return { nullptr, 0 };
}
}  // namespace

void drawOverview(Renderer &renderer, const Snapshot &snap, LinkStatus link) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);

    const auto *codex  = findProvider(snap, ProviderID::Codex);
    const auto *claude = findProvider(snap, ProviderID::Claude);
    const auto *gemini = findProvider(snap, ProviderID::Gemini);

    renderer.drawRing(theme::kCenterX, theme::kCenterY, theme::kRingOuterR,  theme::kRingStroke,
                      theme::kRingTrack, theme::kCodex,  fractionOf(codex));
    renderer.drawRing(theme::kCenterX, theme::kCenterY, theme::kRingMiddleR, theme::kRingStroke,
                      theme::kRingTrack, theme::kClaude, fractionOf(claude));
    renderer.drawRing(theme::kCenterX, theme::kCenterY, theme::kRingInnerR,  theme::kRingStroke,
                      theme::kRingTrack, theme::kGemini, fractionOf(gemini));

    // Center gravity well — anchors the big metric, separates it from inner ring track.
    c.fillCircle(theme::kCenterX, theme::kCenterY, theme::kRingInnerR - theme::kRingStroke - 6,
                 theme::kCenterWell);

    // Center: name + session % of the most-pressured provider.
    const auto *worst = worstOff(snap);
    c.setTextDatum(middle_center);

    if (worst && worst->sessionPct.has_value()) {
        // Provider mark in its full brand color, sitting where the text label used to.
        c.drawBitmap(theme::kCenterX - icons::kSize28 / 2,
                     theme::kCenterY - 40 - icons::kSize28 / 2,
                     icons::bitmap28(worst->id), icons::kSize28, icons::kSize28,
                     theme::colorFor(worst->id));

        // Font7 (7-segment) lacks '%' — draw digits in Font7, then '%' in Font4 next to them.
        char digits[6];
        snprintf(digits, sizeof(digits), "%u", worst->sessionPct.value());
        c.setTextColor(theme::colorFor(worst->id));

        c.setFont(&fonts::Font7);
        int dw = c.textWidth(digits);
        c.setFont(&fonts::Font4);
        int pw = c.textWidth("%");
        constexpr int kGap = 6;
        int leftX = theme::kCenterX - (dw + kGap + pw) / 2;

        c.setTextDatum(middle_left);
        c.setFont(&fonts::Font7);
        c.drawString(digits, leftX, theme::kCenterY + 8);
        c.setFont(&fonts::Font4);
        c.drawString("%", leftX + dw + kGap, theme::kCenterY + 18);
        c.setTextDatum(middle_center);
    } else {
        c.setTextColor(theme::kTextMuted);
        c.setFont(&fonts::Font2);
        c.drawString("SESSION", theme::kCenterX, theme::kCenterY - 32);
        c.setFont(&fonts::Font4);
        c.drawString("--", theme::kCenterX, theme::kCenterY + 8);
    }

    // Bottom strip: live session percentages, in provider colors. Same arrangement as rings (outer→inner).
    c.setFont(&fonts::Font2);
    int by = theme::kCenterY + theme::kRingOuterR - 36;
    auto drawMetric = [&](int x, const ProviderSlot *p, uint32_t color) {
        char buf[8];
        if (p && p->sessionPct.has_value()) snprintf(buf, sizeof(buf), "%u%%", p->sessionPct.value());
        else                                snprintf(buf, sizeof(buf), "--");
        c.setTextColor(color);
        c.drawString(buf, x, by);
    };
    drawMetric(theme::kCenterX - 70, codex,  theme::kCodex);
    drawMetric(theme::kCenterX,      claude, theme::kClaude);
    drawMetric(theme::kCenterX + 70, gemini, theme::kGemini);

    auto pill = pillFor(link, snap);
    renderer.drawPill(theme::kCenterX,
                      theme::kCenterY + theme::kRingOuterR - 8,
                      pill.label, pill.color);
}

}  // namespace stopwatch::views
