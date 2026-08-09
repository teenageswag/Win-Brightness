#pragma once

#include "BrightnessTypes.h"
#include "HardwareBrightness.h"
#include "SoftwareBrightness.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

class BrightnessController {
public:
    BrightnessController();
    ~BrightnessController();

    bool Init();
    void Cleanup();

    int GetBrightness() const;
    void SetBrightness(int percent);

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    void SetBrightnessMode(BrightnessMode mode);
    BrightnessMode GetBrightnessMode() const;

    void SetMonitorSelection(MonitorSelection selection);
    MonitorSelection GetMonitorSelection() const;

    void RefreshMonitors();
    std::vector<MonitorInfo> GetMonitors() const;
    bool IsHardwareAvailableForSelection() const;

private:
    struct ApplyState {
        int brightness = kDefaultBrightness;
        bool enabled = true;
        BrightnessMode mode = BrightnessMode::Software;
        MonitorSelection selection;
        std::vector<MonitorInfo> monitors;
    };

    void QueueApplyLocked();
    void WorkerThreadProc();
    void ApplyBrightness(const ApplyState& state);
    static std::vector<MonitorInfo> ResolveTargets(const ApplyState& state);
    static std::vector<std::wstring> MonitorIds(const std::vector<MonitorInfo>& monitors);

    std::atomic<int> m_currentBrightness{kDefaultBrightness};
    mutable std::mutex m_stateMutex;
    std::condition_variable m_workerCv;
    std::thread m_workerThread;

    bool m_initialized = false;
    bool m_stopWorker = false;
    bool m_applyPending = false;
    bool m_enabled = true;
    BrightnessMode m_mode = BrightnessMode::Software;
    MonitorSelection m_selection;
    std::vector<MonitorInfo> m_monitors;

    bool m_hasApplied = false;
    BrightnessMode m_appliedMode = BrightnessMode::Software;
    std::vector<std::wstring> m_appliedMonitorIds;

    HardwareBrightness m_hardware;
    SoftwareBrightness m_software;
};
