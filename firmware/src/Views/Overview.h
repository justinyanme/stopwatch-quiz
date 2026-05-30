// firmware/src/Views/Overview.h
#pragma once
#include "../SnapshotCodec.h"
#include "../Renderer.h"
#include "../App.h"   // for LinkStatus
#include "../Anim.h"

namespace stopwatch::views {

void drawOverview(Renderer &renderer, const Snapshot &snap, LinkStatus link, const Entrance &anim);

}  // namespace stopwatch::views
