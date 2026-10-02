#pragma once

#include "Renderer.h"

namespace island {
Preferences LoadPreferences();
LSTATUS SavePreferences(const Preferences& preferences);
} // namespace island
