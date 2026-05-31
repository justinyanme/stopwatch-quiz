#pragma once
#include "../CarouselSettings.h"
#include "../Renderer.h"

namespace stopwatch::views {

void drawCarouselSettings(Renderer &renderer, const CarouselSettings &settings,
                          CarouselSettingRow selected);

}  // namespace stopwatch::views
