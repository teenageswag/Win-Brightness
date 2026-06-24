#pragma once
#include "../ui/DimOverlay.h"

class SoftwareBrightness {
public:
    int ReadBrightness() const;
    void ApplyBrightness(int percent);
    void Reset();

private:
    void ApplyGamma(int percent) const;

    DimOverlay m_overlay;
};
