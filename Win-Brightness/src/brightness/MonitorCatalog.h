#pragma once

#include "BrightnessTypes.h"
#include <vector>
#include <expected>

class MonitorCatalog {
public:
    static std::expected<std::vector<MonitorInfo>, DWORD> Enumerate();
};
