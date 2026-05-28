// firmware/src/Views/Provider.cpp
#include "Provider.h"
#include "../App.h"
#include "../IconLookup.h"
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
        case ProviderPlan::Free:       return "FREE";
        case ProviderPlan::Plus:       return "PLUS";
        case ProviderPlan::Pro:        return "PRO";
        case ProviderPlan::Team:       return "TEAM";
        case ProviderPlan::Enterprise: return "ENT";
        default:                       return "";
    }
}

// Per-provider names for the two wire usage slots (`sessionPct` / `weekPct`).
// Codex / Claude: 5-hour session window + rolling week.
// Gemini:         Pro model quota + Flash model quota.
struct SlotLabels { const char *primary; const char *secondary; };
SlotLabels slotLabelsFor(ProviderID id) {
    if (id == ProviderID::Gemini) return { "PRO", "FLASH" };
    return { "SESSION", "WEEK" };
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

struct Pill { const char *label; uint32_t color; };
Pill pillFor(LinkStatus link, const Snapshot &snap) {
    if (link == LinkStatus::NoBridge)            return { "no bridge", theme::kPillInfo };
    if (link == LinkStatus::LinkError)           return { "link error", theme::kPillError };
    if (snap.isProviderMissing())                return { "no source", theme::kPillInfo };
    if (snap.isStale() || snap.isBridgeError())  return { "stale", theme::kPillStale };
    return { nullptr, 0 };
}
}  // namespace

void drawProvider(Renderer &renderer, const Snapshot &snap, ProviderID id, LinkStatus link) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);

    const auto *p = findProvider(snap, id);
    uint32_t color    = theme::colorFor(id);
    uint32_t colorDim = theme::colorDimFor(id);

    SlotLabels labels = slotLabelsFor(id);

    // Top: name + plan. Suppressed for Gemini to avoid colliding with the "PRO" slot label.
    c.setTextDatum(middle_center);
    c.setTextColor(color);
    c.setFont(&fonts::Font2);
    char header[24];
    bool showPlan = p && p->plan != ProviderPlan::Unknown && id != ProviderID::Gemini;
    if (showPlan) {
        snprintf(header, sizeof(header), "%s · %s", labelFor(id), planLabel(p->plan));
    } else {
        snprintf(header, sizeof(header), "%s", labelFor(id));
    }
    c.drawString(header, theme::kCenterX, 24);

    // Rings: outer = session (live), inner = week (dimmed reference)
    int weekRadius = theme::kRingOuterR - theme::kRingStroke * 2 - 4;
    float sessionFrac = (p && p->sessionPct.has_value()) ? p->sessionPct.value() / 100.0f : 0.0f;
    float weekFrac    = (p && p->weekPct.has_value())    ? p->weekPct.value()    / 100.0f : 0.0f;
    renderer.drawRing(theme::kCenterX, theme::kCenterY, theme::kRingOuterR, theme::kRingStroke,
                      theme::kRingTrack, color,    sessionFrac);
    renderer.drawRing(theme::kCenterX, theme::kCenterY, weekRadius,         theme::kRingStroke,
                      theme::kRingTrack, colorDim, weekFrac);

    // Top of center stack: brand mark + cap-type label inline. Icon is the
    // provider tag, text is the cap type. Centered as one unit at y - 36.
    {
        constexpr int kLabelY    = theme::kCenterY - 36;
        constexpr int kIconTextGap = 8;
        c.setFont(&fonts::Font2);
        int tw = c.textWidth(labels.primary);
        int totalW = icons::kSize28 + kIconTextGap + tw;
        int leftX  = theme::kCenterX - totalW / 2;

        c.drawBitmap(leftX, kLabelY - icons::kSize28 / 2,
                     icons::bitmap28(id), icons::kSize28, icons::kSize28, color);

        c.setTextDatum(middle_left);
        c.setTextColor(theme::kTextMuted);
        c.drawString(labels.primary, leftX + icons::kSize28 + kIconTextGap, kLabelY);
        c.setTextDatum(middle_center);
    }

    if (p && p->sessionPct.has_value()) {
        // Font7 (7-segment) lacks '%' — draw digits in Font7, then '%' in Font4 next to them.
        char digits[6];
        snprintf(digits, sizeof(digits), "%u", p->sessionPct.value());
        c.setTextColor(color);

        c.setFont(&fonts::Font7);
        int dw = c.textWidth(digits);
        c.setFont(&fonts::Font4);
        int pw = c.textWidth("%");
        constexpr int kGap = 6;
        int leftX = theme::kCenterX - (dw + kGap + pw) / 2;

        c.setTextDatum(middle_left);
        c.setFont(&fonts::Font7);
        c.drawString(digits, leftX, theme::kCenterY + 4);
        c.setFont(&fonts::Font4);
        c.drawString("%", leftX + dw + kGap, theme::kCenterY + 14);
        c.setTextDatum(middle_center);
    } else {
        c.setTextColor(theme::kTextMuted);
        c.setFont(&fonts::Font4);
        c.drawString("--", theme::kCenterX, theme::kCenterY + 4);
    }

    if (p && p->sessionResetAt.has_value()) {
        char buf[24];
        formatResetIn(buf, sizeof(buf), p->sessionResetAt.value(), snap.capturedAt);
        c.setTextColor(theme::kTextMuted);
        c.setFont(&fonts::Font2);
        c.drawString(buf, theme::kCenterX, theme::kCenterY + 48);
    }

    // Bottom strap: secondary slot label + % + (optional) credits.
    int by = theme::kCenterY + theme::kRingOuterR - 30;
    char bottom[32];
    if (p) {
        if (p->creditsTimesTen.has_value()) {
            snprintf(bottom, sizeof(bottom), "%s %u%% · %u cr",
                     labels.secondary, p->weekPct.value_or(0), p->creditsTimesTen.value() / 10);
        } else if (p->weekPct.has_value()) {
            snprintf(bottom, sizeof(bottom), "%s %u%%", labels.secondary, p->weekPct.value());
        } else {
            bottom[0] = '\0';
        }
        if (bottom[0]) {
            c.setTextColor(theme::kTextMuted);
            c.setFont(&fonts::Font2);
            c.drawString(bottom, theme::kCenterX, by);
        }
    }

    auto pill = pillFor(link, snap);
    renderer.drawPill(theme::kCenterX,
                      theme::kCenterY + theme::kRingOuterR - 8,
                      pill.label, pill.color);
}

}  // namespace stopwatch::views
