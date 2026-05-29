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

    // Identity header (brand mark + name + plan) in the provider colour. Lives in
    // the clear centre, not crammed into the narrow top of the disc, so it can be
    // set at the label tier without grazing the rings or the round bezel.
    // Plan is suppressed for Gemini to avoid colliding with its "PRO" slot label.
    c.setTextDatum(middle_center);
    c.setFont(theme::kFontTitle);
    char header[24];
    bool showPlan = p && p->plan != ProviderPlan::Unknown && id != ProviderID::Gemini;
    if (showPlan) {
        snprintf(header, sizeof(header), "%s · %s", labelFor(id), planLabel(p->plan));
    } else {
        snprintf(header, sizeof(header), "%s", labelFor(id));
    }
    {
        constexpr int kHeaderY  = theme::kCenterY - 74;
        constexpr int kIconGap  = 8;
        int tw    = c.textWidth(header);
        int leftX = theme::kCenterX - (icons::kSize28 + kIconGap + tw) / 2;
        c.drawBitmap(leftX, kHeaderY - icons::kSize28 / 2,
                     icons::bitmap28(id), icons::kSize28, icons::kSize28, color);
        c.setTextDatum(middle_left);
        c.setTextColor(color);
        c.drawString(header, leftX + icons::kSize28 + kIconGap, kHeaderY);
        c.setTextDatum(middle_center);
    }

    // Rings: outer = session (live), inner = week (dimmed reference)
    int weekRadius = theme::kRingOuterR - theme::kRingStroke * 2 - 4;
    float sessionFrac = (p && p->sessionPct.has_value()) ? p->sessionPct.value() / 100.0f : 0.0f;
    float weekFrac    = (p && p->weekPct.has_value())    ? p->weekPct.value()    / 100.0f : 0.0f;
    renderer.drawRing(theme::kCenterX, theme::kCenterY, theme::kRingOuterR, theme::kRingStroke,
                      theme::kRingTrack, color,    sessionFrac);
    renderer.drawRing(theme::kCenterX, theme::kCenterY, weekRadius,         theme::kRingStroke,
                      theme::kRingTrack, colorDim, weekFrac);

    // What the hero number measures, set directly above it. The brand mark now
    // lives in the identity header, so this is the cap-type label on its own.
    c.setFont(theme::kFontTitle);
    c.setTextColor(theme::kTextMuted);
    c.drawString(labels.primary, theme::kCenterX, theme::kCenterY - 30);

    if (p && p->sessionPct.has_value()) {
        // Font7 (7-segment) lacks '%' — draw digits in Font7, then '%' in Font4 next to them.
        char digits[6];
        snprintf(digits, sizeof(digits), "%u", p->sessionPct.value());
        c.setTextColor(color);

        c.setFont(theme::kFontHero);
        int dw = c.textWidth(digits);
        c.setFont(theme::kFontUnit);
        int pw = c.textWidth("%");
        constexpr int kGap = 6;
        int leftX = theme::kCenterX - (dw + kGap + pw) / 2;

        c.setTextDatum(middle_left);
        c.setFont(theme::kFontHero);
        c.drawString(digits, leftX, theme::kCenterY + 16);
        c.setFont(theme::kFontUnit);
        c.drawString("%", leftX + dw + kGap, theme::kCenterY + 26);
        c.setTextDatum(middle_center);
    } else {
        c.setTextColor(theme::kTextMuted);
        c.setFont(theme::kFontUnit);
        c.drawString("--", theme::kCenterX, theme::kCenterY + 16);
    }

    if (p && p->sessionResetAt.has_value()) {
        char buf[24];
        formatResetIn(buf, sizeof(buf), p->sessionResetAt.value(), snap.capturedAt);
        c.setTextColor(theme::kTextMuted);
        c.setFont(theme::kFontBody);
        c.drawString(buf, theme::kCenterX, theme::kCenterY + 60);
    }

    // Secondary slot label + % + (optional) credits. Kept inside the clear centre
    // band (the disc is too narrow for the label tier down at the ring) and above
    // the spend teaser.
    int by = theme::kCenterY + 96;
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
            c.setFont(theme::kFontBody);
            c.drawString(bottom, theme::kCenterX, by);
        }
    }

    auto pill = pillFor(link, snap);
    renderer.drawPill(theme::kCenterX,
                      theme::kCenterY + theme::kRingOuterR - 8,
                      pill.label, pill.color);
}

}  // namespace stopwatch::views
