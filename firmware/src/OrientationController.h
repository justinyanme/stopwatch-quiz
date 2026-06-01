#pragma once
#include <cstdint>

namespace stopwatch {

enum class DisplayOrientation : uint8_t {
    Deg0 = 0,
    Deg90 = 1,
    Deg180 = 2,
    Deg270 = 3,
};

struct OrientationSample {
    float ax;
    float ay;
    float az;
};

class OrientationController {
public:
    static constexpr uint32_t kDebounceMs = 300;

    void begin(uint32_t nowMs, DisplayOrientation initial);
    void reset(uint32_t nowMs, DisplayOrientation orientation);
    bool tick(uint32_t nowMs, OrientationSample sample);
    DisplayOrientation committed() const { return committed_; }

private:
    DisplayOrientation committed_ = DisplayOrientation::Deg0;
    DisplayOrientation pending_ = DisplayOrientation::Deg0;
    uint32_t pendingSinceMs_ = 0;
};

}  // namespace stopwatch
