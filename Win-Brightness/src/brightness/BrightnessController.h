#pragma once

#include "BrightnessTypes.h"
#include "HardwareBrightness.h"
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

class BrightnessController {
public:
    BrightnessController();
    ~BrightnessController();

    static constexpr UINT kMonitorsChangedMessage = WM_APP + 1;
    static constexpr UINT kHardwareStatusMessage = WM_APP + 2;

    bool Init(HWND notificationWindow);
    void Cleanup();

    int GetBrightness() const;
    void SetBrightness(int percent);

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    void SetBrightnessMode(BrightnessMode mode);
    BrightnessMode GetBrightnessMode() const;

    void SetMonitorSelection(MonitorSelection selection);
    MonitorSelection GetMonitorSelection() const;

    void RequestMonitorRefresh();
    std::vector<MonitorInfo> GetMonitors() const;
    bool IsHardwareAvailableForSelection() const;
    DWORD GetCatalogError() const;

private:
    struct ApplyState {
        int brightness = kDefaultBrightness;
        bool enabled = true;
        BrightnessMode mode = BrightnessMode::Software;
        MonitorSelection selection;
        std::vector<MonitorInfo> monitors;
    };

    void QueueApplyLocked(bool resetRetry = true);
    void RefreshMonitors();
    void WorkerThreadProc();
    void ApplyBrightness(const ApplyState& state);
    void PublishWriteResults(const std::vector<HardwareWriteResult>& results);
    void ScheduleRetryLocked();
    static std::vector<MonitorInfo> ResolveTargets(const ApplyState& state);
    static std::vector<std::wstring> MonitorIds(const std::vector<MonitorInfo>& monitors);

    std::atomic<int> m_currentBrightness{kDefaultBrightness};
    mutable std::mutex m_stateMutex;
    std::condition_variable m_workerCv;
    std::thread m_workerThread;

    bool m_initialized = false;
    bool m_stopWorker = false;
    bool m_applyPending = false;
    bool m_refreshPending = false;
    HWND m_notificationWindow = nullptr;
    std::optional<std::chrono::steady_clock::time_point> m_retryAt;
    size_t m_retryCount = 0;
    DWORD m_catalogError = ERROR_SUCCESS;
    bool m_enabled = true;
    BrightnessMode m_mode = BrightnessMode::Software;
    MonitorSelection m_selection;
    std::vector<MonitorInfo> m_monitors;

    // IDs with at least one successful write, retained until restoration succeeds.
    std::vector<std::wstring> m_appliedMonitorIds;

    HardwareBrightness m_hardware;
};
