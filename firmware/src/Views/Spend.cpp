#include "Spend.h"
#include "../IconLookup.h"
#include "../CostFormat.h"
#include "../Theme.h"
#include <cstdio>
#include <cstring>

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

struct Pill { const char *label; uint32_t color; };
Pill pillFor(LinkStatus link, const CostSnapshot &cost) {
    if (link == LinkStatus::NoBridge)            return { "no bridge", theme::kPillInfo };
    if (link == LinkStatus::LinkError)           return { "link error", theme::kPillError };
    if (cost.isUnavailable())                    return { "no cost data", theme::kPillInfo };
    if (cost.isStale() || cost.isBridgeError())  return { "stale", theme::kPillStale };
    return { nullptr, 0 };
}

// Vertical-bar sparkline. `values` length `count`; scaled to `maxVal` (>=1).
void drawSparkline(M5Canvas &c, int x, int y, int w, int h,
                   const int *values, int count, int maxVal, uint32_t color) {
    if (count <= 0 || maxVal < 1) return;
    int barW = w / count;
    if (barW < 1) barW = 1;
    for (int i = 0; i < count; ++i) {
        int bh = (int)((long)values[i] * h / maxVal);
        if (values[i] > 0 && bh < 2) bh = 2;  // floor so a nonzero day is visible
        c.fillRect(x + i * barW, y + (h - bh), barW > 1 ? barW - 1 : 1, bh, color);
    }
}
}  // namespace

void drawTotalSpend(Renderer &renderer, const CostSnapshot &cost, LinkStatus link) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);
    c.setTextDatum(middle_center);

    // Title
    c.setFont(&fonts::Font2);
    c.setTextColor(theme::kTextMuted);
    c.drawString("SPEND & BURN", theme::kCenterX, theme::kCenterY - 96);

    // Aggregate today's cents + 30d cents + 30d tokens; build combined history.
    uint32_t todayCents = 0, monthCents = 0, monthTokens = 0;
    int combined[kCostHistoryDays] = {0};
    int maxCombined = 1;
    bool any = false;
    for (uint8_t i = 0; i < cost.recordCount; ++i) {
        const CostRecord &r = cost.records[i];
        if (r.todayCents) { todayCents += r.todayCents.value(); any = true; }
        if (r.monthCents) monthCents += r.monthCents.value();
        if (r.monthTokens) monthTokens += r.monthTokens.value();
        for (int d = 0; d < kCostHistoryDays; ++d) {
            combined[d] += r.history[d];
            if (combined[d] > maxCombined) maxCombined = combined[d];
        }
    }

    if (any) {
        char hero[16]; formatDollars(todayCents, hero, sizeof(hero), true);
        c.setFont(&fonts::Font7);
        c.setTextColor(theme::kTextPrimary);
        c.drawString(hero, theme::kCenterX, theme::kCenterY - 36);
        c.setFont(&fonts::Font2);
        c.setTextColor(theme::kTextMuted);
        c.drawString("today", theme::kCenterX, theme::kCenterY + 2);

        char tok[16]; humanizeTokens(monthTokens, tok, sizeof(tok));
        char mo[16];  formatDollars(monthCents, mo, sizeof(mo), false);
        char line[40]; snprintf(line, sizeof(line), "30d %s \xC2\xB7 %s", mo, tok);
        c.drawString(line, theme::kCenterX, theme::kCenterY + 28);

        drawSparkline(c, theme::kCenterX - 90, theme::kCenterY + 44, 180, 40,
                      combined, kCostHistoryDays, maxCombined, theme::kTextPrimary);

        // Per-provider split line.
        char split[48] = {0};
        for (uint8_t i = 0; i < cost.recordCount; ++i) {
            const CostRecord &r = cost.records[i];
            char one[24]; char d[16];
            formatDollars(r.todayCents.value_or(0), d, sizeof(d), true);
            snprintf(one, sizeof(one), "%s%.2s %s", (i ? " \xC2\xB7 " : ""), labelFor(r.id), d);
            strncat(split, one, sizeof(split) - strlen(split) - 1);
        }
        c.drawString(split, theme::kCenterX, theme::kCenterY + 96);
    } else {
        c.setFont(&fonts::Font4);
        c.setTextColor(theme::kTextMuted);
        c.drawString("\xE2\x80\x94", theme::kCenterX, theme::kCenterY);
    }

    auto pill = pillFor(link, cost);
    renderer.drawPill(theme::kCenterX, theme::kCenterY + theme::kRingOuterR - 8, pill.label, pill.color);
}

void drawProviderCost(Renderer &renderer, const CostSnapshot &cost, ProviderID id, LinkStatus link) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);
    c.setTextDatum(middle_center);
    uint32_t color = theme::colorFor(id);

    const CostRecord *r = cost.find(id);

    // Header: brand mark + top model.
    {
        c.setFont(&fonts::Font2);
        const char *model = (r && r->topModel[0]) ? r->topModel : labelFor(id);
        int tw = c.textWidth(model);
        int totalW = icons::kSize28 + 8 + tw;
        int leftX = theme::kCenterX - totalW / 2;
        c.drawBitmap(leftX, theme::kCenterY - 96 - icons::kSize28 / 2,
                     icons::bitmap28(id), icons::kSize28, icons::kSize28, color);
        c.setTextDatum(middle_left);
        c.setTextColor(theme::kTextMuted);
        c.drawString(model, leftX + icons::kSize28 + 8, theme::kCenterY - 96);
        c.setTextDatum(middle_center);
    }

    if (r && r->todayCents) {
        char hero[16]; formatDollars(r->todayCents.value(), hero, sizeof(hero), true);
        c.setFont(&fonts::Font7);
        c.setTextColor(color);
        c.drawString(hero, theme::kCenterX, theme::kCenterY - 40);
        c.setFont(&fonts::Font2);
        c.setTextColor(theme::kTextMuted);
        c.drawString("today", theme::kCenterX, theme::kCenterY - 4);

        char mo[16]; formatDollars(r->monthCents.value_or(0), mo, sizeof(mo), false);
        char l1[24]; snprintf(l1, sizeof(l1), "30d   %s", mo);
        c.drawString(l1, theme::kCenterX, theme::kCenterY + 24);
        char tok[16]; humanizeTokens(r->monthTokens.value_or(0), tok, sizeof(tok));
        char l2[24]; snprintf(l2, sizeof(l2), "tok   %s", tok);
        c.drawString(l2, theme::kCenterX, theme::kCenterY + 46);

        int hist[kCostHistoryDays]; int maxV = 1;
        for (int d = 0; d < kCostHistoryDays; ++d) { hist[d] = r->history[d]; if (hist[d] > maxV) maxV = hist[d]; }
        drawSparkline(c, theme::kCenterX - 90, theme::kCenterY + 64, 180, 36,
                      hist, kCostHistoryDays, maxV, color);
    } else {
        c.setFont(&fonts::Font2);
        c.setTextColor(theme::kTextMuted);
        c.drawString("waiting for Mac", theme::kCenterX, theme::kCenterY);
    }

    auto pill = pillFor(link, cost);
    renderer.drawPill(theme::kCenterX, theme::kCenterY + theme::kRingOuterR - 8, pill.label, pill.color);
}

void drawSpendTeaser(M5Canvas &c, const CostRecord *rec, int baselineY, uint32_t color) {
    if (!rec || !rec->todayCents) return;
    char d[16]; formatDollars(rec->todayCents.value(), d, sizeof(d), true);
    char tok[16]; humanizeTokens(rec->todayTokens.value_or(0), tok, sizeof(tok));
    char line[40]; snprintf(line, sizeof(line), "today %s \xC2\xB7 %s", d, tok);
    c.setTextDatum(middle_center);
    c.setTextColor(theme::kTextMuted);
    c.setFont(&fonts::Font2);
    c.drawString(line, theme::kCenterX, baselineY);
    (void)color;
}

}  // namespace stopwatch::views
