// firmware/src/Views/Overview.h
#pragma once
#include "../SnapshotCodec.h"
#include "../Renderer.h"
#include "../App.h"   // for LinkStatus

namespace stopwatch::views {

void drawOverview(Renderer &renderer, const Snapshot &snap, LinkStatus link);

}  // namespace stopwatch::views
