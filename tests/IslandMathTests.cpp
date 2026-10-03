#include "ui/island/Spring.h"
#include "ui/island/Layout.h"
#include <cstdio>
#include <stdexcept>

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() try {
    island::Spring slow, fast;
    slow.Target(420.0);
    fast.Target(420.0);
    for (int i = 0; i < 30; ++i) slow.Advance(1.0 / 60.0);
    for (int i = 0; i < 72; ++i) fast.Advance(1.0 / 144.0);
    Check(std::abs(slow.Value() - fast.Value()) < 0.00001, "refresh-rate independent spring");
    const double position = slow.Value(), velocity = slow.Velocity();
    slow.Target(344.0);
    Check(slow.Value() == position && slow.Velocity() == velocity, "interrupt preserves velocity");
    slow.Advance(1.0 / 144.0);
    Check(std::isfinite(slow.Value()), "interrupted spring stays finite");
    slow.Advance(10.0);
    Check(!slow.Active() && slow.Value() == 344.0, "spring sleeps after settling");
    Check(island::PanelHeight(5, 1000.0f) == island::PanelHeight(8, 1000.0f), "five-row height cap");
    const auto layout = island::Layout::Build(420, island::PanelHeight(8, 1000), 8, 0);
    Check(layout.MaximumScroll() == 3 * island::kRowStride, "overflow rows scroll instead of resizing");
    Check(layout.rows[1].label.top - layout.rows[0].slider.bottom == 15, "row spacing");
    Check(layout.rows[0].slider.top - layout.rows[0].label.bottom == 10, "label spacing");
    Check(layout.viewport.left == 15 && layout.viewport.top == 15, "outer padding");
    const auto narrow = island::Layout::Build(340, island::PanelHeight(5, 1000, 340), 5, 0, 340);
    Check(narrow.power.Width() >= 75 && narrow.hardware.Width() >= 85,
          "buttons remain usable on a narrow DPI-scaled desktop");
    Check(narrow.MaximumScroll() == 0 && narrow.power.top > narrow.hardware.bottom,
          "narrow footer wraps without losing five rows");
    const auto points = island::Squircle(344, 64, 32);
    for (auto point : points) {
        Check(std::isfinite(point.x) && std::isfinite(point.y), "finite superellipse");
        Check(point.x >= -0.0001f && point.x <= 344.0001f && point.y >= -0.0001f && point.y <= 64.0001f,
              "superellipse inside bounds");
    }
    Check(!island::InsideSquircle({0, 0}, {0, 0, 344, 64}, 32), "transparent corner excluded");
    Check(island::InsideSquircle({172, 0}, {0, 0, 344, 64}, 32), "top edge stays attached");
    std::puts("IslandMathTests passed");
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
