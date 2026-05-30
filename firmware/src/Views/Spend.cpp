#include "Spend.h"
#include "../IconLookup.h"
#include "../CostFormat.h"
#include "../Theme.h"
#include "../Version.h"
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
// `animElapsedMs` drives the entrance: bars rise from the baseline left→right
// (Entrance::kSettled once idle → every bar at full height).
void drawSparkline(M5Canvas &c, int x, int y, int w, int h,
                   const int *values, int count, int maxVal, uint32_t color,
                   uint32_t animElapsedMs) {
    if (count <= 0 || maxVal < 1) return;
    int barW = w / count;
    if (barW < 1) barW = 1;
    for (int i = 0; i < count; ++i) {
        int bh = (int)((long)values[i] * h / maxVal);
        if (values[i] > 0 && bh < 2) bh = 2;  // floor so a nonzero day is visible
        bh = (int)(bh * motion::barRise(animElapsedMs, i, count) + 0.5f);  // grow from baseline
        if (bh <= 0) continue;
        c.fillRect(x + i * barW, y + (h - bh), barW > 1 ? barW - 1 : 1, bh, color);
    }
}

// Font7 (7-segment) has no '$' and Font4's 0x24 is a '£' (see Theme.h), so the
// sign is set in kFontDollar (Font2), scaled up (kSignScale) toward the digit
// height and raised like a price superscript, with the big digits in Font7.
// Centered on cx; digits vertically centered on y. textWidth/fontHeight both
// honour the size multiplier, so the layout maths stay correct.
void drawMoneyHero(M5Canvas &c, uint32_t cents, int cx, int y, uint32_t color) {
    char money[16]; formatDollars(cents, money, sizeof(money), true);
    const char *digits = (money[0] == '$') ? money + 1 : money;
    constexpr int kGap = 4;
    constexpr float kSignScale = 2.0f;  // Font2 '$' is small next to Font7 — bump it

    c.setFont(theme::kFontDollar);
    c.setTextSize(kSignScale);
    int signW = c.textWidth("$");
    c.setTextSize(1.0f);
    c.setFont(theme::kFontHero);
    int digitsW   = c.textWidth(digits);
    int digitsTop = y - c.fontHeight() / 2;
    int leftX     = cx - (signW + kGap + digitsW) / 2;

    c.setTextColor(color);
    c.setFont(theme::kFontDollar);
    c.setTextSize(kSignScale);
    c.setTextDatum(top_left);
    c.drawString("$", leftX, digitsTop);
    c.setTextSize(1.0f);
    c.setFont(theme::kFontHero);
    c.setTextDatum(middle_left);
    c.drawString(digits, leftX + signW + kGap, y);
    c.setTextDatum(middle_center);
}

// Draws `s` centered at (cx, y) in `numFont`, but renders its '$' from
// kFontDollar (Font2) — the only built-in face whose 0x24 is a dollar, not a
// '£'. Assumes a single '$', which holds for our currency strings.
void drawDollarLine(M5Canvas &c, const char *s, int cx, int y,
                    const lgfx::RLEfont *numFont, uint32_t color) {
    c.setTextColor(color);
    const char *sign = strchr(s, '$');
    if (!sign) {
        c.setFont(numFont);
        c.setTextDatum(middle_center);
        c.drawString(s, cx, y);
        return;
    }
    char before[48];
    size_t n = (size_t)(sign - s);
    if (n >= sizeof(before)) n = sizeof(before) - 1;
    memcpy(before, s, n);
    before[n] = '\0';
    const char *after = sign + 1;

    c.setFont(numFont);            int wBefore = c.textWidth(before);
    c.setFont(theme::kFontDollar); int wSign   = c.textWidth("$");
    c.setFont(numFont);            int wAfter  = c.textWidth(after);
    int x = cx - (wBefore + wSign + wAfter) / 2;

    c.setTextDatum(middle_left);
    c.setFont(numFont);            c.drawString(before, x, y); x += wBefore;
    c.setFont(theme::kFontDollar); c.drawString("$", x, y);    x += wSign;
    c.setFont(numFont);            c.drawString(after, x, y);
    c.setTextDatum(middle_center);
}

const char *displayName(ProviderID id) {
    switch (id) {
        case ProviderID::Codex:  return "Codex";
        case ProviderID::Claude: return "Claude";
        case ProviderID::Gemini: return "Gemini";
    }
    return "?";
}

// Right-aligned money: "$21.90" with its right edge at rightX; '$' from Font2
// (Font4's 0x24 is a '£'), digits in numFont. Pairs with a left-aligned label
// to make a scannable ledger row.
void drawMoneyRight(M5Canvas &c, uint32_t cents, int rightX, int y,
                    const lgfx::RLEfont *numFont, uint32_t color) {
    char money[16]; formatDollars(cents, money, sizeof(money), true);
    const char *digits = (money[0] == '$') ? money + 1 : money;
    c.setFont(theme::kFontDollar); int wSign = c.textWidth("$");
    c.setFont(numFont);            int wNum  = c.textWidth(digits);
    int x = rightX - wSign - wNum;
    c.setTextColor(color);
    c.setTextDatum(middle_left);
    c.setFont(theme::kFontDollar); c.drawString("$", x, y);
    c.setFont(numFont);            c.drawString(digits, x + wSign, y);
    c.setTextDatum(middle_center);
}
}  // namespace

void drawTotalSpend(Renderer &renderer, const CostSnapshot &cost, LinkStatus link, const Entrance &anim) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);
    c.setTextDatum(middle_center);
    uint32_t e = anim.elapsed();

    // Title
    c.setFont(theme::kFontTitle);
    c.setTextColor(theme::kTextMuted);
    c.drawString("SPEND & BURN", theme::kCenterX, 52);

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
        // Hero: combined spend today — the one number you glance for, counting up.
        uint32_t heroCents = (uint32_t)(todayCents * motion::countUp(e) + 0.5f);
        drawMoneyHero(c, heroCents, theme::kCenterX, 118, theme::kTextPrimary);
        c.setFont(theme::kFontBody);
        c.setTextColor(theme::kTextMuted);
        c.drawString("today", theme::kCenterX, 154);

        // Per-provider breakdown: readable ledger rows, each in its provider's color.
        // (Replaces the old micro-font split line — the part that was hard to read.)
        constexpr int kDotX = 132, kNameX = 152, kAmtRightX = 334;
        int rowY = 200;
        for (uint8_t i = 0; i < cost.recordCount; ++i) {
            const CostRecord &r = cost.records[i];
            uint32_t pc = theme::colorFor(r.id);
            c.fillCircle(kDotX, rowY, 6, pc);
            c.setFont(theme::kFontBody);
            c.setTextColor(theme::kTextPrimary);
            c.setTextDatum(middle_left);
            c.drawString(displayName(r.id), kNameX, rowY);
            c.setTextDatum(middle_center);
            if (r.todayCents) {
                drawMoneyRight(c, r.todayCents.value(), kAmtRightX, rowY, theme::kFontBody, pc);
            } else {
                // codexbar has no price for today's model (e.g. a brand-new model it
                // doesn't know yet), so today's cost is unknown — show an honest em-dash,
                // never a fake "$0.00". Mirrors the per-provider cost view's convention.
                c.setFont(theme::kFontBody);
                const char *dash = "\xE2\x80\x94";  // em dash
                c.setTextColor(pc);
                c.setTextDatum(middle_left);
                c.drawString(dash, kAmtRightX - c.textWidth(dash), rowY);
                c.setTextDatum(middle_center);
            }
            rowY += 40;
        }

        // 30-day burn: secondary context, held muted so the breakdown leads.
        char tok[16]; humanizeTokens(monthTokens, tok, sizeof(tok));
        char mo[16];  formatDollars(monthCents, mo, sizeof(mo), false);
        char line[40]; snprintf(line, sizeof(line), "30d  %s \xC2\xB7 %s", mo, tok);
        int ctxY = rowY + 16;
        drawDollarLine(c, line, theme::kCenterX, ctxY, theme::kFontBody, theme::kTextMuted);
        // Width matches the per-provider chart (6px bars) so the two spend
        // screens read alike; taller too. At most 2 ledger rows (codex+claude),
        // so the chart sits high enough to grow without nearing pill or ring.
        drawSparkline(c, theme::kCenterX - 105, ctxY + 22, 210, 46,
                      combined, kCostHistoryDays, maxCombined, theme::kTextMuted, e);
    } else {
        c.setFont(theme::kFontUnit);
        c.setTextColor(theme::kTextMuted);
        c.drawString("\xE2\x80\x94", theme::kCenterX, theme::kCenterY);
    }

    auto pill = pillFor(link, cost);
    renderer.drawPill(theme::kCenterX, theme::kCenterY + theme::kRingOuterR - 8, pill.label, pill.color);

    // Firmware release version — a discreet footer just below the status pill
    // (which lands at y=425); kFontMicro/Font2 keeps it unobtrusive. The y=448
    // baseline is provisional: confirm on-device it clears the pill and bezel.
    char ver[24];
    snprintf(ver, sizeof(ver), "v%s", kFirmwareVersion);
    c.setFont(theme::kFontMicro);
    c.setTextColor(theme::kTextMuted);
    c.setTextDatum(middle_center);
    c.drawString(ver, theme::kCenterX, 448);
}

void drawProviderCost(Renderer &renderer, const CostSnapshot &cost, ProviderID id, LinkStatus link, const Entrance &anim) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);
    c.setTextDatum(middle_center);
    uint32_t color = theme::colorFor(id);
    uint32_t e = anim.elapsed();

    const CostRecord *r = cost.find(id);

    // Header: brand mark + top model.
    {
        c.setFont(theme::kFontTitle);
        const char *model = (r && r->topModel[0]) ? r->topModel : labelFor(id);
        int tw = c.textWidth(model);
        int totalW = icons::kSize28 + 8 + tw;
        int leftX = theme::kCenterX - totalW / 2;
        c.drawBitmap(leftX, theme::kCenterY - 100 - icons::kSize28 / 2,
                     icons::bitmap28(id), icons::kSize28, icons::kSize28, color);
        c.setTextDatum(middle_left);
        c.setTextColor(theme::kTextMuted);
        c.drawString(model, leftX + icons::kSize28 + 8, theme::kCenterY - 100);
        c.setTextDatum(middle_center);
    }

    // "waiting for Mac" is only honest when there's truly nothing yet. A record
    // that merely lacks *today's* dollar cost — e.g. today's model isn't priced
    // upstream, so codexbar returns a null session cost — still carries 30-day
    // spend, token counts and history worth showing; don't collapse that into a
    // bogus connection message.
    bool anyData = r && (r->todayCents || r->monthCents || r->todayTokens || r->monthTokens);
    if (!anyData) {
        c.setFont(theme::kFontBody);
        c.setTextColor(theme::kTextMuted);
        c.drawString("waiting for Mac", theme::kCenterX, theme::kCenterY);
    } else {
        // Hero: today's spend when priced. When it isn't, an honest em-dash plus a
        // note — a missing dollar should read as "not priced", not "no spend" — and
        // we still surface today's token activity so the screen isn't a dead end.
        if (r->todayCents) {
            uint32_t heroCents = (uint32_t)(r->todayCents.value() * motion::countUp(e) + 0.5f);
            drawMoneyHero(c, heroCents, theme::kCenterX, theme::kCenterY - 44, color);
            c.setFont(theme::kFontBody);
            c.setTextColor(theme::kTextMuted);
            c.drawString("today", theme::kCenterX, theme::kCenterY - 2);
        } else {
            c.setFont(theme::kFontUnit);
            c.setTextColor(theme::kTextMuted);
            c.drawString("\xE2\x80\x94", theme::kCenterX, theme::kCenterY - 44);  // em dash
            char sub[28];
            if (r->todayTokens) {
                char tk[16]; humanizeTokens(r->todayTokens.value(), tk, sizeof(tk));
                snprintf(sub, sizeof(sub), "today \xC2\xB7 %s tok", tk);
            } else {
                snprintf(sub, sizeof(sub), "today \xC2\xB7 not priced");
            }
            c.setFont(theme::kFontBody);
            c.setTextColor(theme::kTextMuted);
            c.drawString(sub, theme::kCenterX, theme::kCenterY - 2);
        }

        // 30-day context + history: shown whenever present, not gated on today.
        c.setTextDatum(middle_center);
        if (r->monthCents) {
            char mo[16]; formatDollars(r->monthCents.value(), mo, sizeof(mo), false);
            char l1[24]; snprintf(l1, sizeof(l1), "30d   %s", mo);
            drawDollarLine(c, l1, theme::kCenterX, theme::kCenterY + 30, theme::kFontBody, theme::kTextMuted);
        }
        if (r->monthTokens) {
            char tk[16]; humanizeTokens(r->monthTokens.value(), tk, sizeof(tk));
            char l2[24]; snprintf(l2, sizeof(l2), "tok   %s", tk);
            c.setFont(theme::kFontBody);
            c.setTextColor(theme::kTextMuted);
            c.drawString(l2, theme::kCenterX, theme::kCenterY + 62);
        }

        int hist[kCostHistoryDays]; int maxV = 1;
        for (int d = 0; d < kCostHistoryDays; ++d) { hist[d] = r->history[d]; if (hist[d] > maxV) maxV = hist[d]; }
        // Bigger sparkline: taller bars carry the trend, a touch wider so they
        // don't read as thin spikes. Bottom corners stay ~r173 from center —
        // clear of the ring (inner edge ~r192) even when the status pill shows.
        drawSparkline(c, theme::kCenterX - 105, theme::kCenterY + 88, 210, 52,
                      hist, kCostHistoryDays, maxV, color, e);
    }

    auto pill = pillFor(link, cost);
    renderer.drawPill(theme::kCenterX, theme::kCenterY + theme::kRingOuterR - 8, pill.label, pill.color);
}

}  // namespace stopwatch::views
