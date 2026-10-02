#include "FrameClock.h"
#include <dwmapi.h>
#include <array>
#include <chrono>

#pragma comment(lib, "dwmapi.lib")

namespace island {
FrameClock::~FrameClock() { Stop(); }
void FrameClock::Stop() {
    if (m_stop) SetEvent(m_stop);
    if (m_thread.joinable()) m_thread.join();
    for (auto handle : {m_ready, m_request, m_stop}) if (handle) CloseHandle(handle);
    m_stop = m_request = m_ready = nullptr;
}
bool FrameClock::Start(HANDLE dxgiFrame) {
    Stop();
    m_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_request = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_stop || !m_request || !m_ready) { Stop(); return false; }
    try {
        m_thread = std::thread([this, dxgiFrame] {
            const std::array<HANDLE, 2> requests{m_stop, m_request};
            const std::array<HANDLE, 2> frames{m_stop, dxgiFrame};
            while (WaitForMultipleObjects(2, requests.data(), FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
                if (WaitForMultipleObjects(2, frames.data(), FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
                // Composition swap chains can signal latency before scanout.
                // DwmFlush supplies the compositor boundary on this UI-only
                // pacing thread. No renderer or monitor state is shared here.
                if (FAILED(DwmFlush())) {
                    // A composition failure must not turn into an idle CPU loop.
                    if (WaitForSingleObject(m_stop, 16) == WAIT_OBJECT_0) break;
                }
                if (WaitForSingleObject(m_stop, 0) == WAIT_OBJECT_0) break;
                SetEvent(m_ready);
            }
        });
    } catch (...) { Stop(); return false; }
    return true;
}
void FrameClock::Request() { if (m_request) SetEvent(m_request); }
} // namespace island
