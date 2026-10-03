#pragma once
#include <cstdint>

namespace island {
class IdleDismissal {
public:
    static constexpr std::uint32_t kInterval = 3500;
    void Reset(std::uint64_t now) { m_deadline = now + kInterval; }
    void Cancel() { m_deadline = 0; }
    bool Expired(std::uint64_t now) const { return m_deadline != 0 && now >= m_deadline; }
    std::uint32_t Remaining(std::uint64_t now) const {
        return m_deadline == 0 ? 0 : now >= m_deadline ? 1 : static_cast<std::uint32_t>(m_deadline - now);
    }
private:
    std::uint64_t m_deadline = 0;
};
} // namespace island
