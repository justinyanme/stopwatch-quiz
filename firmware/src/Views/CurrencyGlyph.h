#pragma once
#include "../BalanceFormat.h"
#include "../Theme.h"
#include <M5Unified.h>

namespace stopwatch::views {

inline const char *currencyGlyphText(const char *code) {
    return code ? code : "";
}

inline int currencyGlyphWidth(M5Canvas &c, const char *code) {
    switch (currencyGlyphKind(code)) {
        case CurrencyGlyphKind::Dollar:
            c.setFont(theme::kFontDollar);
            return c.textWidth("$");
        case CurrencyGlyphKind::Yen:
            c.setFont(theme::kFontTitle);
            return c.textWidth("Y");
        case CurrencyGlyphKind::Text:
            c.setFont(theme::kFontTitle);
            return c.textWidth(currencyGlyphText(code));
    }
    return 0;
}

// '$' is drawn from Font2 because larger bundled fonts render that codepoint as
// '£'. No bundled font carries '¥', so CNY/JPY draw a Y with two bars.
inline int drawCurrencyGlyph(M5Canvas &c, const char *code, int rightX, int y, uint32_t color) {
    c.setTextColor(color);
    c.setTextDatum(middle_right);
    switch (currencyGlyphKind(code)) {
        case CurrencyGlyphKind::Dollar:
            c.setFont(theme::kFontDollar);
            c.drawString("$", rightX, y);
            return c.textWidth("$");
        case CurrencyGlyphKind::Yen: {
            c.setFont(theme::kFontTitle);
            int w = c.textWidth("Y");
            c.drawString("Y", rightX, y);
            c.fillRect(rightX - w, y - 1, w, 2, color);
            c.fillRect(rightX - w, y + 5, w, 2, color);
            return w;
        }
        case CurrencyGlyphKind::Text:
            c.setFont(theme::kFontTitle);
            c.drawString(currencyGlyphText(code), rightX, y);
            return c.textWidth(currencyGlyphText(code));
    }
    return 0;
}

}  // namespace stopwatch::views
