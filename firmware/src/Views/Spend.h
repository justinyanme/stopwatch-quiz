// firmware/src/Views/Spend.h
#pragma once
#include "../CostCodec.h"
#include "../Renderer.h"
#include "../App.h"
#include "../Anim.h"

namespace stopwatch::views {

void drawTotalSpend(Renderer &renderer, const CostSnapshot &cost, LinkStatus link, const Entrance &anim);
void drawProviderCost(Renderer &renderer, const CostSnapshot &cost, ProviderID id, LinkStatus link, const Entrance &anim);

}  // namespace stopwatch::views
