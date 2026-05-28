// firmware/src/Views/Overview.h
#pragma once
#include "../SnapshotCodec.h"
#include "../Renderer.h"

namespace stopwatch::views {

/// Draws the three-ring overview into the renderer's sprite.
/// Caller must call renderer.present() afterward.
void drawOverview(Renderer &renderer, const Snapshot &snap);

}  // namespace stopwatch::views
