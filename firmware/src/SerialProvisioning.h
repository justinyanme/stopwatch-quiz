#pragma once
#include "DeviceConfig.h"

namespace stopwatch {

class SerialProvisioning {
public:
    void begin();
    void poll();
    bool load(DeviceNetworkConfig &out);
    void clear();

private:
    void applyLine(const char *line);
    void printConfig();
    bool saveString(const char *key, const char *value);
    bool clearStore();
    bool open_ = false;
};

}  // namespace stopwatch
