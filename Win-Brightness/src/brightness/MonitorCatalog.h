#pragma once

#include "BrightnessTypes.h"
#include <vector>

class MonitorCatalog {
public:
    static std::vector<MonitorInfo> Enumerate();
};
