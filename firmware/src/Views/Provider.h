// firmware/src/Views/Provider.h
#pragma once
#include "../SnapshotCodec.h"
#include "../Renderer.h"
#include "../App.h"
#include "../Anim.h"

namespace stopwatch::views {

void drawProvider(Renderer &renderer, const Snapshot &snap, ProviderID id, LinkStatus link, const Entrance &anim);

}  // namespace stopwatch::views
