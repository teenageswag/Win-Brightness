#define NOMINMAX
#include "SoftwareBrightness.h"
#include "../platform/Win32Helpers.h"
#include "BrightnessTypes.h"
#include <cmath>

namespace {
    constexpr double kGammaCurve = 1.0;
    constexpr double kMinGammaFactor = 0.002;
} // namespace

int SoftwareBrightness::ReadBrightness() const {
    WORD ramp[3][256];
    ScreenDc screenDc;
    if (!screenDc.Get()) {
        return 50;
    }

    if (!GetDeviceGammaRamp(screenDc.Get(), ramp)) {
        return 50;
    }

    const int percent = static_cast<int>(ramp[0][255] * 100.0 / 65535.0 + 0.5);
    return ClampBrightness(percent);
}

void SoftwareBrightness::ApplyBrightness(int percent) {
    const int clamped = ClampBrightness(percent);
    ApplyGamma(clamped);
    m_overlay.Apply(clamped);
}

void SoftwareBrightness::Reset() {
    ApplyGamma(kMaxBrightness);
    m_overlay.Destroy();
}

void SoftwareBrightness::ApplyGamma(int percent) const {
    const int clamped = ClampBrightness(percent);
    double factor = std::pow(clamped / 100.0, kGammaCurve);
    factor = (std::max)(factor, kMinGammaFactor);

    WORD ramp[3][256];
    for (int i = 0; i < 256; ++i) {
        const double scaled = (std::min)((i / 255.0) * factor, 1.0);
        const WORD value = static_cast<WORD>(scaled * 65535.0 + 0.5);
        ramp[0][i] = ramp[1][i] = ramp[2][i] = value;
    }

    ScreenDc screenDc;
    if (screenDc.Get()) {
        SetDeviceGammaRamp(screenDc.Get(), ramp);
    }
}
