// firmware/src/Views/Overview.cpp
#include "Overview.h"
#include "../App.h"
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

    // Center: worst-off provider's % in its color.
    const auto *worst = worstOff(snap);
    c.setTextDatum(middle_center);
    c.setTextColor(theme::kTextMuted);
    c.setFont(&fonts::Font2);
    c.drawString("Most used", theme::kCenterX, theme::kCenterY - 36);

    if (worst && worst->sessionPct.has_value()) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u%%", worst->sessionPct.value());
        c.setTextColor(theme::colorFor(worst->id));
        c.setFont(&fonts::Font7);
        c.drawString(buf, theme::kCenterX, theme::kCenterY + 4);
    } else {
        c.setTextColor(theme::kTextMuted);
        c.setFont(&fonts::Font4);
        c.drawString("--", theme::kCenterX, theme::kCenterY + 4);
    }

    // Bottom legend chips.
    c.setFont(&fonts::Font2);
    int by = theme::kCenterY + theme::kRingOuterR - 36;
    c.setTextColor(theme::kCodex);  c.drawString("\xE2\x97\x8F CX", theme::kCenterX - 60, by);
    c.setTextColor(theme::kClaude); c.drawString("\xE2\x97\x8F CL", theme::kCenterX,      by);
    c.setTextColor(theme::kGemini); c.drawString("\xE2\x97\x8F GM", theme::kCenterX + 60, by);

    auto pill = pillFor(link, snap);
    renderer.drawPill(theme::kCenterX,
                      theme::kCenterY + theme::kRingOuterR - 8,
                      pill.label, pill.color);
}

}  // namespace stopwatch::views
