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

// Digit string only (no symbol): "318.50" / "--" (unknown) / "∞" (unlimited).
void balanceNumber(const BalanceRecord &r, char *buf, size_t n) {
    if (r.unlimited)     { snprintf(buf, n, "\xE2\x88\x9E"); return; }
    if (!r.balanceMinor) { snprintf(buf, n, "--"); return; }
    formatBalanceMinor(r.balanceMinor.value(), r.decimals, buf, n);
}

// Draws the currency symbol with its right edge at rightX, vertically centered on y.
// '$' is drawn from Font2 — Font4's 0x24 glyph is a '£' (see Theme.h). No bundled font
// carries '¥', so it's built from the body-font 'Y' plus two horizontal bars. Any other
// code renders as its ISO text. Returns the pixel width drawn.
int drawCurrencyGlyph(M5Canvas &c, const char *code, int rightX, int y, uint32_t color) {
    c.setTextColor(color);
    c.setTextDatum(middle_right);
    if (strcmp(code, "USD") == 0) {
        c.setFont(theme::kFontDollar);
        c.drawString("$", rightX, y);
        return c.textWidth("$");
    }
    if (strcmp(code, "CNY") == 0 || strcmp(code, "JPY") == 0) {
        c.setFont(theme::kFontTitle);
        int w = c.textWidth("Y");
        c.drawString("Y", rightX, y);
        c.fillRect(rightX - w, y - 1, w, 2, color);
        c.fillRect(rightX - w, y + 5, w, 2, color);
        return w;
    }
    c.setFont(theme::kFontTitle);
    c.drawString(code, rightX, y);
    return c.textWidth(code);
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

        char num[16]; balanceNumber(r, num, sizeof(num));
        uint32_t balColor = dim ? theme::kTextMuted : (r.low ? theme::kPillStale : color);
        c.setFont(theme::kFontTitle);
        c.setTextColor(balColor);
        c.setTextDatum(middle_right);
        c.drawString(num, 396, rowY);                          // digits right-aligned
        int wNum = c.textWidth(num);
        drawCurrencyGlyph(c, r.currency, 396 - wNum - 3, rowY, balColor);  // symbol to its left

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
