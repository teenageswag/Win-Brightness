#pragma once

#include "BrightnessTypes.h"
#include "../ui/DimOverlay.h"
#include <vector>

class SoftwareBrightness {
public:
    void ApplyBrightness(int percent, const std::vector<MonitorInfo>& monitors);
    void ApplyBrightness(const std::vector<MonitorInfo>& monitors);
    void Reset();

private:
    DimOverlay m_overlay;
};
