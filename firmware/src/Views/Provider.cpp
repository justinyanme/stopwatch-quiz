// firmware/src/Views/Provider.cpp
#include "Provider.h"
#include "../Theme.h"
#include <cstdio>
#include <time.h>

namespace stopwatch::views {

namespace {
const char *labelFor(ProviderID id) {
    switch (id) {
        case ProviderID::Codex:  return "CODEX";
        case ProviderID::Claude: return "CLAUDE";
        case ProviderID::Gemini: return "GEMINI";
    }
    return "?";
}

const char *planLabel(ProviderPlan plan) {
    switch (plan) {
        case ProviderPlan::Free:       return "Free";
        case ProviderPlan::Plus:       return "Plus";
        case ProviderPlan::Pro:        return "Pro";
        case ProviderPlan::Team:       return "Team";
        case ProviderPlan::Enterprise: return "Ent";
        default:                       return "";
    }
}

void formatResetIn(char *buf, size_t bufSize, uint32_t resetAt, uint32_t now) {
    if (resetAt == 0 || resetAt <= now) { snprintf(buf, bufSize, "—"); return; }
    uint32_t delta = resetAt - now;
    if (delta < 3600)       snprintf(buf, bufSize, "%um left", delta / 60);
    else if (delta < 86400) snprintf(buf, bufSize, "%uh %02um left", delta / 3600, (delta % 3600) / 60);
    else                    snprintf(buf, bufSize, "%ud left", delta / 86400);
}

const ProviderSlot *findProvider(const Snapshot &snap, ProviderID id) {
    for (uint8_t i = 0; i < snap.providerCount; ++i) {
        if (snap.providers[i].id == id) return &snap.providers[i];
    }
    return nullptr;
}
}  // namespace

void drawProvider(Renderer &renderer, const Snapshot &snap, ProviderID id) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);

    const auto *p = findProvider(snap, id);
    uint32_t color    = theme::colorFor(id);
    uint32_t colorDim = theme::colorDimFor(id);

    // Top: name + plan
    c.setTextDatum(middle_center);
    c.setTextColor(color);
    c.setFont(&fonts::Font2);
    char header[24];
    if (p && p->plan != ProviderPlan::Unknown) {
        snprintf(header, sizeof(header), "%s · %s", labelFor(id), planLabel(p->plan));
    } else {
        snprintf(header, sizeof(header), "%s", labelFor(id));
    }
    c.drawString(header, theme::kCenterX, 24);

    // Rings: outer = session, inner = week
    float sessionFrac = (p && p->sessionPct.has_value()) ? p->sessionPct.value() / 100.0f : 0.0f;
    float weekFrac    = (p && p->weekPct.has_value())    ? p->weekPct.value()    / 100.0f : 0.0f;
    renderer.drawRing(theme::kCenterX, theme::kCenterY, theme::kRingOuterR,  theme::kRingStroke,
                      theme::kRingTrack, color,    sessionFrac);
    renderer.drawRing(theme::kCenterX, theme::kCenterY, theme::kRingOuterR - theme::kRingStroke * 2 - 4,
                      theme::kRingStroke, theme::kRingTrack, colorDim, weekFrac);

    // Center: session %
    c.setTextColor(theme::kTextMuted);
    c.setFont(&fonts::Font2);
    c.drawString("Session", theme::kCenterX, theme::kCenterY - 30);
    if (p && p->sessionPct.has_value()) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u%%", p->sessionPct.value());
        c.setTextColor(color);
        c.setFont(&fonts::Font7);
        c.drawString(buf, theme::kCenterX, theme::kCenterY + 6);
    } else {
        c.setTextColor(theme::kTextMuted);
        c.setFont(&fonts::Font4);
        c.drawString("--", theme::kCenterX, theme::kCenterY + 6);
    }

    // Reset countdown under session
    if (p && p->sessionResetAt.has_value()) {
        char buf[24];
        formatResetIn(buf, sizeof(buf), p->sessionResetAt.value(), snap.capturedAt);
        c.setTextColor(theme::kTextMuted);
        c.setFont(&fonts::Font2);
        c.drawString(buf, theme::kCenterX, theme::kCenterY + 60);
    }

    // Bottom strap: week % + credits
    int by = theme::kCenterY + theme::kRingOuterR - 30;
    char bottom[32];
    if (p) {
        if (p->creditsTimesTen.has_value()) {
            snprintf(bottom, sizeof(bottom), "Week %u%% · %u cr",
                     p->weekPct.value_or(0), p->creditsTimesTen.value() / 10);
        } else if (p->weekPct.has_value()) {
            snprintf(bottom, sizeof(bottom), "Week %u%%", p->weekPct.value());
        } else {
            bottom[0] = '\0';
        }
        if (bottom[0]) {
            c.setTextColor(theme::kTextMuted);
            c.setFont(&fonts::Font2);
            c.drawString(bottom, theme::kCenterX, by);
        }
    }
}

}  // namespace stopwatch::views
