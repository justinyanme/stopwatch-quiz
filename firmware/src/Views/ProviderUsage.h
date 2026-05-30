#pragma once
#include "../UsageCodec.h"
#include "../BalanceCodec.h"
#include "../Renderer.h"
#include "../App.h"
#include "../Anim.h"

namespace stopwatch::views {

/// Per-provider usage detail. `bal` supplies the balance hero (always present);
/// `usage` supplies chart totals/status for usage-capable providers.
/// `metric` selects the chart series. `anim` drives the entrance.
void drawProviderUsage(Renderer &renderer, const BalanceRecord &bal,
                       const UsageSnapshot &usage, UsageMetric metric,
                       LinkStatus link, const Entrance &anim);

}  // namespace stopwatch::views
