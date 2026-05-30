#include "ProviderUsage.h"
#include "../BalanceFormat.h"
#include "../CostFormat.h"
#include "../Theme.h"
#include "CurrencyGlyph.h"
#include <cstdio>
#include <cstring>

namespace stopwatch::views {

namespace {

// Vertical-bar sparkline rising from baseline left→right (mirrors Spend.cpp
// drawSparkline, adapted for uint8_t history arrays from UsageRecord).
void drawBars(M5Canvas &c, int x, int y, int w, int h,
              const uint8_t *values, int count, int maxVal,
              uint32_t color, uint32_t animElapsedMs) {
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

const char *providerLabel(BalanceKind k) {
    switch (k) {
        case BalanceKind::OpenRouter:  return "OPENROUTER";
        case BalanceKind::DeepSeek:    return "DEEPSEEK";
        case BalanceKind::Groq:        return "GROQ";
        case BalanceKind::Together:    return "TOGETHER";
        case BalanceKind::Fireworks:   return "FIREWORKS";
        case BalanceKind::SiliconFlow: return "SILICONFLOW";
        case BalanceKind::Moonshot:    return "MOONSHOT";
        case BalanceKind::Zhipu:       return "ZHIPU";
        default:                       return "USAGE";
    }
}

const char *usageMessage(const UsageSnapshot &snapshot, const UsageRecord *usage) {
    if (snapshot.isUnavailable() || snapshot.isPendingEmpty()) return "usage unavailable";
    if (snapshot.isBridgeError()) return "usage refresh failed";
    if (snapshot.isStale()) return "usage data stale";
    if (!usage) return "usage data unavailable";
    switch (usage->status) {
        case BalanceStatus::AuthError:   return "usage auth error";
        case BalanceStatus::Unreachable: return "usage offline";
        case BalanceStatus::Stale:       return "usage data stale";
        case BalanceStatus::Depleted:    return "usage depleted";
        default:                         return "usage data unavailable";
    }
}

struct Pill { const char *label; uint32_t color; };
Pill pillFor(LinkStatus link, const UsageSnapshot &snapshot, const UsageRecord *usage) {
    if (link == LinkStatus::NoBridge)              return { "no bridge", theme::kPillInfo };
    if (link == LinkStatus::LinkError)             return { "link error", theme::kPillError };
    if (snapshot.isUnavailable() || snapshot.isPendingEmpty()) return { "no usage", theme::kPillInfo };
    if (snapshot.isStale() || snapshot.isBridgeError())        return { "usage stale", theme::kPillStale };
    if (!usage) return { nullptr, 0 };
    switch (usage->status) {
        case BalanceStatus::AuthError:   return { "auth error", theme::kPillError };
        case BalanceStatus::Unreachable: return { "offline", theme::kPillError };
        case BalanceStatus::Stale:       return { "usage stale", theme::kPillStale };
        case BalanceStatus::Depleted:    return { "depleted", theme::kPillStale };
        default:                         return { nullptr, 0 };
    }
}

void drawCenteredAmount(M5Canvas &c, const char *amount, const char *currency,
                        bool showCurrency, int centerX, int y, uint32_t color) {
    c.setFont(theme::kFontHero);
    int amountW = c.textWidth(amount);
    int glyphW = showCurrency ? currencyGlyphWidth(c, currency) : 0;
    int gap = glyphW > 0 ? 8 : 0;
    int amountRight = centerX + (amountW + gap + glyphW) / 2;

    if (glyphW > 0) {
        drawCurrencyGlyph(c, currency, amountRight - amountW - gap, y, color);
    }

    c.setTextDatum(middle_right);
    c.setFont(theme::kFontHero);
    c.setTextColor(color);
    c.drawString(amount, amountRight, y);
    c.setTextDatum(middle_center);
}

void drawUsageTotalLine(M5Canvas &c, const char *amount, const char *currency,
                        const char *tokens, int centerX, int y) {
    const char *prefix = "30d ";
    char suffix[24];
    snprintf(suffix, sizeof(suffix), " \xC2\xB7 %s tok", tokens);

    c.setFont(theme::kFontBody);
    int prefixW = c.textWidth(prefix);
    int amountW = c.textWidth(amount);
    int suffixW = c.textWidth(suffix);
    int glyphW = currencyGlyphWidth(c, currency);
    int gap = glyphW > 0 ? 3 : 0;
    int x = centerX - (prefixW + glyphW + gap + amountW + suffixW) / 2;

    c.setTextDatum(middle_left);
    c.setFont(theme::kFontBody);
    c.setTextColor(theme::kTextMuted);
    c.drawString(prefix, x, y);
    x += prefixW;

    if (glyphW > 0) {
        drawCurrencyGlyph(c, currency, x + glyphW, y, theme::kTextMuted);
        x += glyphW + gap;
    }

    c.setTextDatum(middle_left);
    c.setFont(theme::kFontBody);
    c.setTextColor(theme::kTextMuted);
    c.drawString(amount, x, y);
    c.drawString(suffix, x + amountW, y);
    c.setTextDatum(middle_center);
}

}  // namespace

void drawProviderUsage(Renderer &renderer, const BalanceRecord &bal,
                       const UsageSnapshot &snapshot, UsageMetric metric,
                       LinkStatus link, const Entrance &anim) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);
    c.setTextDatum(middle_center);
    uint32_t e = anim.elapsed();
    uint32_t color = theme::balanceColorFor(bal.kind);
    const UsageRecord *usage = snapshot.find(bal.kind);
    const bool hasChartData = snapshot.hasFreshSuccessfulData(bal.kind);

    // Header: provider name.
    c.setFont(theme::kFontTitle);
    c.setTextColor(theme::kTextMuted);
    c.drawString(providerLabel(bal.kind), theme::kCenterX, theme::kCenterY - 88);

    // Hero: current balance.
    char num[16];
    if (bal.unlimited)         snprintf(num, sizeof(num), "\xE2\x88\x9E");
    else if (bal.balanceMinor) formatBalanceMinor(bal.balanceMinor.value(), bal.decimals, num, sizeof(num));
    else                       snprintf(num, sizeof(num), "--");
    drawCenteredAmount(c, num, bal.currency, bal.balanceMinor.has_value() && !bal.unlimited,
                       theme::kCenterX, theme::kCenterY - 44, color);
    c.setFont(theme::kFontBody);
    c.setTextColor(theme::kTextMuted);
    c.drawString("balance", theme::kCenterX, theme::kCenterY - 4);

    if (hasChartData) {
        // Totals line: 30d cost · tokens.
        if (usage->monthCostMinor) {
            char mo[16]; formatBalanceMinor(usage->monthCostMinor.value(), usage->decimals, mo, sizeof(mo));
            char tk[16]; humanizeTokens(usage->monthTokens.value_or(0), tk, sizeof(tk));
            const char *currency = usage->currency[0] ? usage->currency : bal.currency;
            drawUsageTotalLine(c, mo, currency, tk, theme::kCenterX, theme::kCenterY + 28);
        }

        // Chart: cost or tokens, each scaled to its own max.
        const uint8_t *series = (metric == UsageMetric::Cost)
                                    ? usage->costHistory
                                    : usage->tokenHistory;
        int maxV = 1;
        for (int d = 0; d < kUsageHistoryDays; ++d)
            if (series[d] > maxV) maxV = series[d];

        c.setFont(theme::kFontMicro);
        c.setTextColor(theme::kTextMuted);
        c.drawString(metric == UsageMetric::Cost ? "30-DAY COST" : "30-DAY TOKENS",
                     theme::kCenterX, theme::kCenterY + 52);
        drawBars(c, theme::kCenterX - 105, theme::kCenterY + 66, 210, 52,
                 series, kUsageHistoryDays, maxV, color, e);
    } else {
        c.setFont(theme::kFontBody);
        c.setTextColor(theme::kTextMuted);
        c.drawString(usageMessage(snapshot, usage), theme::kCenterX, theme::kCenterY + 40);
    }

    auto pill = pillFor(link, snapshot, usage);
    renderer.drawPill(theme::kCenterX, theme::kCenterY + theme::kRingOuterR - 8, pill.label, pill.color);
}

}  // namespace stopwatch::views
