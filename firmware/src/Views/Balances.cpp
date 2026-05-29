#include "Balances.h"
#include "../BalanceFormat.h"
#include "../Theme.h"
#include <cstdio>
#include <cstring>
#include <cctype>

namespace stopwatch::views {

namespace {
constexpr int kListTop    = 86;    // first row baseline band
constexpr int kListBottom = 410;   // above the pill
constexpr int kRowPitch   = 44;    // Font4 row spacing

struct Pill { const char *label; uint32_t color; };
Pill pillFor(LinkStatus link, const BalanceSnapshot &bal) {
    if (link == LinkStatus::NoBridge)              return { "no bridge", theme::kPillInfo };
    if (link == LinkStatus::LinkError)             return { "link error", theme::kPillError };
    if (bal.isStale() || bal.isBridgeError())      return { "stale", theme::kPillStale };
    return { nullptr, 0 };
}

const char *statusMarker(BalanceStatus s) {
    switch (s) {
        case BalanceStatus::AuthError:   return "auth";
        case BalanceStatus::Unreachable: return "offline";
        case BalanceStatus::Stale:       return "stale";
        case BalanceStatus::Depleted:    return "empty";
        default:                          return nullptr;
    }
}

// "$42.10" / "¥318.50" / "—" / "∞" into buf.
void formatBalance(const BalanceRecord &r, char *buf, size_t n) {
    if (r.unlimited)            { snprintf(buf, n, "%s\xE2\x88\x9E", currencySymbol(r.currency)); return; }
    if (!r.balanceMinor)        { snprintf(buf, n, "\xE2\x80\x94"); return; }  // —
    char num[16]; formatBalanceMinor(r.balanceMinor.value(), r.decimals, num, sizeof(num));
    snprintf(buf, n, "%s%s", currencySymbol(r.currency), num);
}
}  // namespace

int drawBalances(Renderer &renderer, const BalanceSnapshot &bal, LinkStatus link, int scrollOffset) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);

    c.setTextDatum(middle_center);
    c.setFont(theme::kFontTitle);
    c.setTextColor(theme::kTextMuted);
    c.drawString("API \xC2\xB7 BALANCES", theme::kCenterX, 52);

    const int contentH = bal.recordCount * kRowPitch;

    if (bal.recordCount == 0) {
        c.setFont(theme::kFontBody);
        c.setTextColor(theme::kTextMuted);
        c.drawString("no providers", theme::kCenterX, theme::kCenterY);
    }

    for (uint8_t i = 0; i < bal.recordCount; ++i) {
        const BalanceRecord &r = bal.records[i];
        int rowY = kListTop + i * kRowPitch - scrollOffset;
        if (rowY < kListTop - kRowPitch || rowY > kListBottom) continue;  // off-screen

        uint32_t color = theme::balanceColorFor(r.kind);
        bool dim = (r.status != BalanceStatus::Ok);

        int leftX = 70;
        if (r.kind == BalanceKind::Generic) {
            c.fillRoundRect(leftX - 9, rowY - 9, 20, 20, 4, theme::kTextMuted);
            char chip[3] = { (char)toupper(r.name[0]), r.name[0] ? (char)toupper(r.name[1]) : '\0', 0 };
            c.setFont(theme::kFontMicro); c.setTextColor(theme::kBackground); c.setTextDatum(middle_center);
            c.drawString(chip, leftX + 1, rowY);
        } else {
            c.fillCircle(leftX, rowY, 6, color);
        }

        c.setFont(theme::kFontTitle);
        c.setTextColor(dim ? theme::kTextMuted : theme::kTextPrimary);
        c.setTextDatum(middle_left);
        c.drawString(r.name, leftX + 24, rowY);

        char balStr[24]; formatBalance(r, balStr, sizeof(balStr));
        uint32_t balColor = dim ? theme::kTextMuted : (r.low ? theme::kPillStale : color);
        c.setTextColor(balColor);
        c.setTextDatum(middle_right);
        c.drawString(balStr, 396, rowY);

        if (const char *m = statusMarker(r.status)) {
            c.setFont(theme::kFontMicro); c.setTextColor(theme::kTextMuted); c.setTextDatum(middle_left);
            c.drawString(m, leftX + 24, rowY + 15);
        }
    }

    int maxOffset = contentH - (kListBottom - kListTop);
    if (maxOffset > 0) {
        int trackH = kListBottom - kListTop;
        int barH = trackH * (kListBottom - kListTop) / contentH;
        int barY = kListTop + (trackH - barH) * scrollOffset / maxOffset;
        c.fillRoundRect(450, kListTop, 4, trackH, 2, theme::kRingTrack);
        c.fillRoundRect(450, barY, 4, barH, 2, theme::kTextMuted);
    }

    auto pill = pillFor(link, bal);
    renderer.drawPill(theme::kCenterX, theme::kCenterY + theme::kRingOuterR - 8, pill.label, pill.color);
    c.setTextDatum(middle_center);
    return contentH;
}

}  // namespace stopwatch::views
