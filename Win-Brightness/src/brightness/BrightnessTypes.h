#pragma once
#include <algorithm>

enum class BrightnessMode {
	Hardware = 0,
	Software = 1
};

inline constexpr int kMinBrightness = 1;
inline constexpr int kMaxBrightness = 100;

inline int ClampBrightness(int percent) {
	return std::clamp(percent, kMinBrightness, kMaxBrightness);
}
