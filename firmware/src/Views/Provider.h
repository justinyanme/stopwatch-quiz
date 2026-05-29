// firmware/src/Views/Provider.h
#pragma once
#include "../SnapshotCodec.h"
#include "../CostCodec.h"
#include "../Renderer.h"
#include "../App.h"

namespace stopwatch::views {

void drawProvider(Renderer &renderer, const Snapshot &snap, ProviderID id, LinkStatus link,
                  const CostRecord *cost = nullptr);

}  // namespace stopwatch::views
