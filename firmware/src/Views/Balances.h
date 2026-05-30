#pragma once
#include "../BalanceCodec.h"
#include "../Renderer.h"
#include "../App.h"

namespace stopwatch::views {

/// Scrollable wallet list. `scrollOffset` (px, ≥0) shifts rows up. Returns the
/// total content height in pixels so the caller can set scroll bounds.
int drawBalances(Renderer &renderer, const BalanceSnapshot &bal, LinkStatus link, int scrollOffset);

/// Visible height of the ledger row viewport used by drawBalances().
int balancesViewportHeight();

/// Returns the record index under screen-y `y` given `scrollOffset`, or -1 if the
/// tap is outside any row / the viewport. `count` is the number of records.
int balanceRowAtY(int y, int scrollOffset, int count);

}  // namespace stopwatch::views
