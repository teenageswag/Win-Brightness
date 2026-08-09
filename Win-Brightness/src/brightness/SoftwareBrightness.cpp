#include "SoftwareBrightness.h"

void SoftwareBrightness::ApplyBrightness(int percent, const std::vector<MonitorInfo>& monitors) {
    m_overlay.Apply(ClampBrightness(percent), monitors);
}

void SoftwareBrightness::Reset() {
    m_overlay.Destroy();
}
