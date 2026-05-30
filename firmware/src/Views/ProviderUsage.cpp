#include "ProviderUsage.h"
#include "../BalanceFormat.h"
#include "../CostFormat.h"
#include "../Theme.h"
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

}  // namespace

void drawProviderUsage(Renderer &renderer, const BalanceRecord &bal,
                       const UsageRecord *usage, UsageMetric metric,
                       LinkStatus link, const Entrance &anim) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);
    c.setTextDatum(middle_center);
    uint32_t e = anim.elapsed();
    uint32_t color = theme::balanceColorFor(bal.kind);

    // Header: provider name.
    c.setFont(theme::kFontTitle);
    c.setTextColor(theme::kTextMuted);
    c.drawString(providerLabel(bal.kind), theme::kCenterX, theme::kCenterY - 88);

    // Hero: current balance.
    char num[16];
    if (bal.unlimited)         snprintf(num, sizeof(num), "\xE2\x88\x9E");
    else if (bal.balanceMinor) formatBalanceMinor(bal.balanceMinor.value(), bal.decimals, num, sizeof(num));
    else                       snprintf(num, sizeof(num), "--");
    c.setFont(theme::kFontHero);
    c.setTextColor(color);
    c.drawString(num, theme::kCenterX, theme::kCenterY - 44);
    c.setFont(theme::kFontBody);
    c.setTextColor(theme::kTextMuted);
    c.drawString("balance", theme::kCenterX, theme::kCenterY - 4);

    if (usage) {
        // Totals line: 30d cost · tokens.
        if (usage->monthCostMinor) {
            char mo[16]; formatBalanceMinor(usage->monthCostMinor.value(), usage->decimals, mo, sizeof(mo));
            char tk[16]; humanizeTokens(usage->monthTokens.value_or(0), tk, sizeof(tk));
            char line[40]; snprintf(line, sizeof(line), "30d %s \xC2\xB7 %s tok", mo, tk);
            c.setFont(theme::kFontBody);
            c.setTextColor(theme::kTextMuted);
            c.drawString(line, theme::kCenterX, theme::kCenterY + 28);
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
        c.drawString("usage data unavailable", theme::kCenterX, theme::kCenterY + 40);
    }

    // Status pill (only when there's a link problem).
    const char *pill = (link == LinkStatus::NoBridge)  ? "no bridge"
                     : (link == LinkStatus::LinkError) ? "link error" : nullptr;
    uint32_t pillColor = (link == LinkStatus::NoBridge) ? theme::kPillInfo : theme::kPillError;
    renderer.drawPill(theme::kCenterX, theme::kCenterY + theme::kRingOuterR - 8, pill, pillColor);
}

}  // namespace stopwatch::views
