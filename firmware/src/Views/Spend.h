// firmware/src/Views/Spend.h
#pragma once
#include "../CostCodec.h"
#include "../Renderer.h"
#include "../App.h"

namespace stopwatch::views {

void drawTotalSpend(Renderer &renderer, const CostSnapshot &cost, LinkStatus link);
void drawProviderCost(Renderer &renderer, const CostSnapshot &cost, ProviderID id, LinkStatus link);

}  // namespace stopwatch::views
