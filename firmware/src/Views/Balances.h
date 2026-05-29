#pragma once
#include "../BalanceCodec.h"
#include "../Renderer.h"
#include "../App.h"

namespace stopwatch::views {

/// Scrollable wallet list. `scrollOffset` (px, ≥0) shifts rows up. Returns the
/// total content height in pixels so the caller can set scroll bounds.
int drawBalances(Renderer &renderer, const BalanceSnapshot &bal, LinkStatus link, int scrollOffset);

}  // namespace stopwatch::views
