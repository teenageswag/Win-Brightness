#pragma once

#include <windows.h>
#include <thread>

namespace island {
class FrameClock {
public:
    FrameClock() = default;
    ~FrameClock();
    FrameClock(const FrameClock&) = delete;
    FrameClock& operator=(const FrameClock&) = delete;
    bool Start(HANDLE dxgiFrame);
    void Stop();
    void Request();
    HANDLE Ready() const { return m_ready; }
private:
    HANDLE m_stop = nullptr, m_request = nullptr, m_ready = nullptr;
    std::thread m_thread;
};
} // namespace island
