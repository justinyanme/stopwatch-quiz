#include "Balances.h"
#include "../BalanceFormat.h"
#include "../Theme.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cmath>

namespace stopwatch::views {

namespace {
constexpr int kViewportTop    = 94;
constexpr int kViewportBottom = 386;
constexpr int kRowHeight      = 44;
constexpr int kRowPitch       = 52;
constexpr int kRoundRadius    = 8;
constexpr int kScrollTop      = 142;
constexpr int kScrollHeight   = 188;
constexpr int kScrollX        = 410;

constexpr uint32_t kBalanceBg       = 0x06090C;
constexpr uint32_t kHeaderChip      = 0x111821;
constexpr uint32_t kRowFill         = 0x0C1218;
constexpr uint32_t kRowFillLow      = 0x20160D;
constexpr uint32_t kRowFillDisabled = 0x090D11;
constexpr uint32_t kChipFill        = 0x19222B;
constexpr uint32_t kLowText         = 0xFFB37A;

struct Pill { const char *label; uint32_t color; };
Pill pillFor(LinkStatus link, const BalanceSnapshot &bal) {
    if (link == LinkStatus::NoBridge)              return { "no bridge", theme::kPillInfo };
    if (link == LinkStatus::LinkError)             return { "link error", theme::kPillError };
    if (bal.isStale() || bal.isBridgeError())      return { "stale", theme::kPillStale };
    return { nullptr, 0 };
}

const char *statusMarker(BalanceStatus s) {
    switch (s) {
        case BalanceStatus::AuthError:   return "AUTH";
        case BalanceStatus::Unreachable: return "OFFLINE";
        case BalanceStatus::Stale:       return "STALE";
        case BalanceStatus::Depleted:    return "EMPTY";
        default:                          return nullptr;
    }
}

const char *rowCaption(const BalanceRecord &r) {
    if (r.status != BalanceStatus::Ok) return statusMarker(r.status);
    if (r.low) return "LOW";
    return nullptr;
}

bool isUnavailable(BalanceStatus s) {
    return s == BalanceStatus::AuthError ||
           s == BalanceStatus::Unreachable ||
           s == BalanceStatus::Stale;
}

int contentHeight(uint8_t count) {
    if (count == 0) return 0;
    return kRowHeight + (count - 1) * kRowPitch;
}

void rowFrame(int rowY, int &x, int &w) {
    constexpr int kSafeR = theme::kRingOuterR - 10;
    int dy = rowY - theme::kCenterY;
    if (dy < 0) dy = -dy;
    int half = 146;
    if (dy < kSafeR) {
        half = (int)std::sqrt((double)(kSafeR * kSafeR - dy * dy));
    }

    int left  = theme::kCenterX - half + 16;
    int right = theme::kCenterX + half - 16;
    if (left < 48) left = 48;
    if (right > 418) right = 418;
    if (right - left < 236) {
        int mid = (left + right) / 2;
        left = mid - 118;
        right = mid + 118;
    }
    x = left;
    w = right - left;
}

void fitText(M5Canvas &c, const char *src, int maxW, char *out, size_t n) {
    if (n == 0) return;
    if (!src || !src[0]) {
        snprintf(out, n, "?");
        return;
    }
    snprintf(out, n, "%s", src);
    if (c.textWidth(out) <= maxW) return;

    size_t len = strlen(out);
    while (len > 0) {
        if (len + 2 < n) {
            out[len] = '.';
            out[len + 1] = '.';
            out[len + 2] = '\0';
        } else {
            out[len] = '\0';
        }
        if (c.textWidth(out) <= maxW) return;
        out[len] = '\0';
        --len;
    }
    snprintf(out, n, "..");
}

void initials(const char *name, char *out) {
    out[0] = name && name[0] ? (char)toupper((unsigned char)name[0]) : '?';
    out[1] = name && name[1] ? (char)toupper((unsigned char)name[1]) : '\0';
    out[2] = '\0';
}

// Digit string only: "318.50", "--" for unknown, infinity for unlimited.
void balanceNumber(const BalanceRecord &r, char *buf, size_t n) {
    if (r.unlimited)     { snprintf(buf, n, "\xE2\x88\x9E"); return; }
    if (!r.balanceMinor) { snprintf(buf, n, "--"); return; }
    formatBalanceMinor(r.balanceMinor.value(), r.decimals, buf, n);
}

int currencyGlyphWidth(M5Canvas &c, const char *code) {
    if (strcmp(code, "USD") == 0) {
        c.setFont(theme::kFontDollar);
        return c.textWidth("$");
    }
    if (strcmp(code, "CNY") == 0 || strcmp(code, "JPY") == 0) {
        c.setFont(theme::kFontTitle);
        return c.textWidth("Y");
    }
    c.setFont(theme::kFontTitle);
    return c.textWidth(code);
}

// Draws the currency symbol with its right edge at rightX, vertically centered on y.
// '$' is drawn from Font2. Font4's 0x24 glyph is a '£' (see Theme.h). No bundled font
// carries '¥', so it is built from the body-font 'Y' plus two horizontal bars. Any other
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

void drawHeader(M5Canvas &c, const BalanceSnapshot &bal) {
    c.setTextDatum(middle_center);
    c.setFont(theme::kFontMicro);
    c.setTextColor(theme::kTextMuted);
    c.drawString("API", theme::kCenterX, 34);

    c.setFont(theme::kFontTitle);
    c.setTextColor(theme::kTextPrimary);
    c.drawString("BALANCES", theme::kCenterX, 57);

    char count[16];
    snprintf(count, sizeof(count), "%u KEY%s", bal.recordCount, bal.recordCount == 1 ? "" : "S");
    c.setFont(theme::kFontMicro);
    int w = c.textWidth(count) + 18;
    c.fillRoundRect(theme::kCenterX - w / 2, 72, w, 22, kRoundRadius, kHeaderChip);
    c.setTextColor(theme::kTextMuted);
    c.drawString(count, theme::kCenterX, 83);
}

void drawIdentity(M5Canvas &c, const BalanceRecord &r, int x, int y, uint32_t color, bool dim) {
    if (r.kind == BalanceKind::Generic) {
        c.fillRoundRect(x - 12, y - 12, 24, 24, 6, dim ? theme::kRingTrack : kChipFill);
        char chip[3]; initials(r.name, chip);
        c.setFont(theme::kFontMicro);
        c.setTextColor(dim ? theme::kTextMuted : theme::kTextPrimary);
        c.setTextDatum(middle_center);
        c.drawString(chip, x, y + 1);
    } else {
        c.fillCircle(x, y, 10, theme::kRingTrack);
        c.fillCircle(x, y, 6, dim ? theme::kTextMuted : color);
    }
}
}  // namespace

int balancesViewportHeight() {
    return kViewportBottom - kViewportTop;
}

int drawBalances(Renderer &renderer, const BalanceSnapshot &bal, LinkStatus link, int scrollOffset) {
    auto &c = renderer.canvas();
    renderer.clear(kBalanceBg);

    drawHeader(c, bal);

    const int contentH = contentHeight(bal.recordCount);

    if (bal.recordCount == 0) {
        c.fillRoundRect(102, 188, 262, 92, kRoundRadius, kRowFill);
        c.setFont(theme::kFontTitle);
        c.setTextColor(theme::kTextPrimary);
        c.setTextDatum(middle_center);
        c.drawString("NO PROVIDERS", theme::kCenterX, 220);
        c.setFont(theme::kFontMicro);
        c.setTextColor(theme::kTextMuted);
        c.drawString("KEYCHAIN EMPTY", theme::kCenterX, 248);
    }

    for (uint8_t i = 0; i < bal.recordCount; ++i) {
        const BalanceRecord &r = bal.records[i];
        int rowY = kViewportTop + kRowHeight / 2 + i * kRowPitch - scrollOffset;
        if (rowY + kRowHeight / 2 < kViewportTop || rowY - kRowHeight / 2 > kViewportBottom) continue;

        uint32_t color = theme::balanceColorFor(r.kind);
        bool dim = isUnavailable(r.status);
        bool caution = r.low || r.status == BalanceStatus::Depleted;

        int rowX, rowW;
        rowFrame(rowY, rowX, rowW);
        c.fillRoundRect(rowX, rowY - kRowHeight / 2, rowW, kRowHeight,
                        kRoundRadius, dim ? kRowFillDisabled : (caution ? kRowFillLow : kRowFill));

        int markX = rowX + 25;
        drawIdentity(c, r, markX, rowY, caution ? kLowText : color, dim);

        char num[16]; balanceNumber(r, num, sizeof(num));
        c.setFont(theme::kFontTitle);
        int wNum = c.textWidth(num);
        int wCur = currencyGlyphWidth(c, r.currency);
        int balanceRight = rowX + rowW - 18;
        int balanceLeft = balanceRight - wNum - (wCur ? wCur + 4 : 0);
        int nameX = markX + 24;
        int maxNameW = balanceLeft - nameX - 12;
        if (maxNameW < 40) maxNameW = 40;

        const char *caption = rowCaption(r);
        c.setFont(theme::kFontTitle);
        c.setTextColor(dim ? theme::kTextMuted : theme::kTextPrimary);
        c.setTextDatum(middle_left);
        char fittedName[18];
        fitText(c, r.name, maxNameW, fittedName, sizeof(fittedName));
        c.drawString(fittedName, nameX, caption ? rowY - 8 : rowY);

        if (caption) {
            c.setFont(theme::kFontMicro);
            c.setTextColor(caution && !dim ? kLowText : theme::kTextMuted);
            c.drawString(caption, nameX, rowY + 14);
        }

        uint32_t balColor = dim ? theme::kTextMuted : (caution ? kLowText : color);
        c.setFont(theme::kFontTitle);
        c.setTextColor(balColor);
        c.setTextDatum(middle_right);
        c.drawString(num, balanceRight, rowY);
        drawCurrencyGlyph(c, r.currency, balanceRight - wNum - 4, rowY, balColor);
    }

    int maxOffset = contentH - balancesViewportHeight();
    if (maxOffset > 0) {
        int barH = kScrollHeight * balancesViewportHeight() / contentH;
        if (barH < 28) barH = 28;
        int barY = kScrollTop + (kScrollHeight - barH) * scrollOffset / maxOffset;
        c.fillRoundRect(kScrollX, kScrollTop, 4, kScrollHeight, 2, theme::kRingTrack);
        c.fillRoundRect(kScrollX, barY, 4, barH, 2, theme::kTextMuted);
    }

    auto pill = pillFor(link, bal);
    renderer.drawPill(theme::kCenterX, theme::kCenterY + theme::kRingOuterR - 8, pill.label, pill.color);
    c.setTextDatum(middle_center);
    return contentH;
}

}  // namespace stopwatch::views
