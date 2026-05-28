// firmware/src/Views/Provider.h
#pragma once
#include "../SnapshotCodec.h"
#include "../Renderer.h"

namespace stopwatch::views {

/// Draws the per-provider concentric-rings screen for `id` from `snap`.
/// If the provider is missing in `snap`, draws a placeholder.
void drawProvider(Renderer &renderer, const Snapshot &snap, ProviderID id);

}  // namespace stopwatch::views
