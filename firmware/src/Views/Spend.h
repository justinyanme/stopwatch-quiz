// firmware/src/Views/Spend.h
#pragma once
#include "../CostCodec.h"
#include "../Renderer.h"
#include "../App.h"

namespace stopwatch::views {

void drawTotalSpend(Renderer &renderer, const CostSnapshot &cost, LinkStatus link);
void drawProviderCost(Renderer &renderer, const CostSnapshot &cost, ProviderID id, LinkStatus link);

/// Teaser line ("today $X · NNNm") drawn by the provider ring screen. No-op if rec is null.
void drawSpendTeaser(M5Canvas &c, const CostRecord *rec, int baselineY, uint32_t color);

}  // namespace stopwatch::views
