#pragma once

#include <algorithm>
#include <cmath>

namespace island {
struct SpringConfig {
    double stiffness;
    double damping;
    double mass = 1.0;
};
inline constexpr SpringConfig kMorphSpring{440.0, 42.0};
inline constexpr SpringConfig kFeedbackSpring{980.0, 63.0};
inline constexpr SpringConfig kSliderSpring{1540.0, 79.0};

class Spring {
public:
    explicit Spring(double value = 0.0, SpringConfig config = kMorphSpring)
        : m_value(value), m_target(value), m_config(config) {}

    double Value() const { return m_value; }
    double Velocity() const { return m_velocity; }
    double Target() const { return m_target; }
    void Target(double value) { if (std::isfinite(value)) m_target = value; }
    void Snap(double value) {
        m_value = m_target = value;
        m_velocity = 0.0;
    }
    bool Active(double epsilon = 0.001) const {
        return std::abs(m_value - m_target) > epsilon || std::abs(m_velocity) > epsilon * 10.0;
    }

    void Advance(double seconds, double epsilon = 0.001) {
        if (!std::isfinite(seconds) || seconds <= 0.0) return;
        // Solve m*x'' + c*x' + k*(x-target) = 0 analytically. A new target
        // preserves both presentation position and velocity, even mid-reversal.
        const double dt = std::min(seconds, 10.0);
        const double y = m_value - m_target;
        const double a = m_config.damping / (2.0 * m_config.mass);
        const double w2 = m_config.stiffness / m_config.mass;
        const double discriminant = a * a - w2;
        double nextY = 0.0;
        double nextV = 0.0;
        if (std::abs(discriminant) < 0.000001) {
            const double b = m_velocity + a * y;
            const double decay = std::exp(-a * dt);
            nextY = (y + b * dt) * decay;
            nextV = (m_velocity - a * b * dt) * decay;
        } else if (discriminant > 0.0) {
            const double root = std::sqrt(discriminant);
            const double r1 = -a + root;
            const double r2 = -a - root;
            const double c1 = (m_velocity - r2 * y) / (r1 - r2);
            const double c2 = y - c1;
            const double e1 = std::exp(r1 * dt);
            const double e2 = std::exp(r2 * dt);
            nextY = c1 * e1 + c2 * e2;
            nextV = r1 * c1 * e1 + r2 * c2 * e2;
        } else {
            const double w = std::sqrt(-discriminant);
            const double b = (m_velocity + a * y) / w;
            const double cosine = std::cos(w * dt);
            const double sine = std::sin(w * dt);
            const double decay = std::exp(-a * dt);
            nextY = (y * cosine + b * sine) * decay;
            nextV = ((b * w - a * y) * cosine - (y * w + a * b) * sine) * decay;
        }
        m_value = m_target + nextY;
        m_velocity = nextV;
        if (!Active(epsilon)) Snap(m_target);
    }

private:
    double m_value;
    double m_target;
    double m_velocity = 0.0;
    SpringConfig m_config;
};
} // namespace island
