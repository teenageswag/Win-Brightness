#include "BrightnessController.h"
#include "MonitorCatalog.h"
#include <algorithm>
#include <unordered_set>

BrightnessController::BrightnessController() = default;

BrightnessController::~BrightnessController() {
    Cleanup();
}

bool BrightnessController::Init(HWND notificationWindow) {
    {
        std::lock_guard lock(m_stateMutex);
        if (m_initialized) {
            return true;
        }
        m_initialized = true;
        m_stopWorker = false;
        m_refreshPending = true;
        m_notificationWindow = notificationWindow;
    }

    try {
        m_workerThread = std::thread(&BrightnessController::WorkerThreadProc, this);
    } catch (const std::system_error&) {
        std::lock_guard lock(m_stateMutex);
        m_initialized = false;
        m_notificationWindow = nullptr;
        return false;
    }
    m_workerCv.notify_one();
    return true;
}

void BrightnessController::Cleanup() {
    {
        std::lock_guard lock(m_stateMutex);
        if (!m_initialized) {
            return;
        }
        m_initialized = false;
        m_stopWorker = true;
    }

    m_workerCv.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

}

int BrightnessController::GetBrightness() const {
    return m_currentBrightness.load(std::memory_order_relaxed);
}

void BrightnessController::SetBrightness(int percent) {
    m_currentBrightness.store(ClampBrightness(percent), std::memory_order_relaxed);
    {
        std::lock_guard lock(m_stateMutex);
        QueueApplyLocked();
    }
    m_workerCv.notify_one();
}

void BrightnessController::SetEnabled(bool enabled) {
    {
        std::lock_guard lock(m_stateMutex);
        if (m_enabled == enabled) {
            return;
        }
        m_enabled = enabled;
        QueueApplyLocked();
    }
    m_workerCv.notify_one();
}

bool BrightnessController::IsEnabled() const {
    std::lock_guard lock(m_stateMutex);
    return m_enabled;
}

void BrightnessController::SetBrightnessMode(BrightnessMode mode) {
    {
        std::lock_guard lock(m_stateMutex);
        if (m_mode == mode) {
            return;
        }
        m_mode = mode;
        QueueApplyLocked();
    }
    m_workerCv.notify_one();
}

BrightnessMode BrightnessController::GetBrightnessMode() const {
    std::lock_guard lock(m_stateMutex);
    return m_mode;
}

void BrightnessController::SetMonitorSelection(MonitorSelection selection) {
    std::erase_if(selection.ids, [](const std::wstring& id) { return id.empty(); });
    std::ranges::sort(selection.ids);
    selection.ids.erase(std::unique(selection.ids.begin(), selection.ids.end()), selection.ids.end());

    {
        std::lock_guard lock(m_stateMutex);
        if (m_selection.all == selection.all && m_selection.ids == selection.ids) {
            return;
        }
        m_selection = std::move(selection);
        QueueApplyLocked();
    }
    m_workerCv.notify_one();
}

MonitorSelection BrightnessController::GetMonitorSelection() const {
    std::lock_guard lock(m_stateMutex);
    return m_selection;
}

void BrightnessController::RequestMonitorRefresh() {
    {
        std::lock_guard lock(m_stateMutex);
        m_refreshPending = true;
    }
    m_workerCv.notify_one();
}

void BrightnessController::RefreshMonitors() {
    std::vector<MonitorInfo> monitors = MonitorCatalog::Enumerate();
    m_hardware.RefreshMonitors(monitors);

    {
        std::lock_guard lock(m_stateMutex);
        m_monitors = std::move(monitors);
        QueueApplyLocked();
    }
    m_workerCv.notify_one();
    PostMessageW(m_notificationWindow, kMonitorsChangedMessage, 0, 0);
}

std::vector<MonitorInfo> BrightnessController::GetMonitors() const {
    std::lock_guard lock(m_stateMutex);
    return m_monitors;
}

bool BrightnessController::IsHardwareAvailableForSelection() const {
    std::lock_guard lock(m_stateMutex);
    return std::ranges::any_of(m_monitors, [this](const MonitorInfo& monitor) {
        return m_selection.Contains(monitor.id) && monitor.hardwareBrightness;
    });
}

void BrightnessController::QueueApplyLocked() {
    m_applyPending = true;
}

void BrightnessController::WorkerThreadProc() {
    while (true) {
        ApplyState state;
        {
            std::unique_lock lock(m_stateMutex);
            m_workerCv.wait(lock, [this] {
                return m_stopWorker || m_refreshPending || m_applyPending;
            });
            if (m_stopWorker) {
                break;
            }

            if (m_refreshPending) {
                m_refreshPending = false;
                lock.unlock();
                RefreshMonitors();
                continue;
            }

            m_applyPending = false;
            state.brightness = m_currentBrightness.load(std::memory_order_relaxed);
            state.enabled = m_enabled;
            state.mode = m_mode;
            state.selection = m_selection;
            state.monitors = m_monitors;
        }

        ApplyBrightness(state);
    }

    m_hardware.ReleaseMonitors();
}

void BrightnessController::ApplyBrightness(const ApplyState& state) {
    const std::vector<MonitorInfo> targets = ResolveTargets(state);
    const std::vector<std::wstring> targetIds = MonitorIds(targets);

    std::vector<std::wstring> removedIds;
    for (const auto& id : m_appliedMonitorIds) {
        if (state.mode != BrightnessMode::Hardware ||
            std::ranges::find(targetIds, id) == targetIds.end()) {
            removedIds.push_back(id);
        }
    }
    if (!removedIds.empty()) {
        const auto restored = m_hardware.ApplyBrightness(kMaxBrightness, removedIds);
        for (const auto& id : removedIds) {
            const bool any = std::ranges::any_of(restored, [&](const auto& result) {
                return result.monitorId == id;
            });
            const bool failed = std::ranges::any_of(restored, [&](const auto& result) {
                return result.monitorId == id && result.error != ERROR_SUCCESS;
            });
            if (any && !failed) std::erase(m_appliedMonitorIds, id);
        }
        PublishWriteResults(restored);
    }

    if (state.mode == BrightnessMode::Hardware) {
        const auto written = m_hardware.ApplyBrightness(
            state.enabled ? state.brightness : kMaxBrightness, targetIds);
        for (const auto& result : written) {
            if (result.error == ERROR_SUCCESS &&
                std::ranges::find(m_appliedMonitorIds, result.monitorId) == m_appliedMonitorIds.end()) {
                m_appliedMonitorIds.push_back(result.monitorId);
            }
        }
        PublishWriteResults(written);
    }
}

void BrightnessController::PublishWriteResults(const std::vector<HardwareWriteResult>& results) {
    bool changed = false;
    {
        std::lock_guard lock(m_stateMutex);
        for (auto& monitor : m_monitors) {
            bool found = false;
            DWORD error = ERROR_SUCCESS;
            for (const auto& result : results) {
                if (result.monitorId != monitor.id) continue;
                found = true;
                if (result.error != ERROR_SUCCESS && error == ERROR_SUCCESS) error = result.error;
            }
            if (found && error != monitor.hardwareError) {
                monitor.hardwareError = error;
                changed = true;
            }
        }
    }
    if (changed) PostMessageW(m_notificationWindow, kHardwareStatusMessage, 0, 0);
}

std::vector<MonitorInfo> BrightnessController::ResolveTargets(const ApplyState& state) {
    std::vector<MonitorInfo> targets;
    for (const MonitorInfo& monitor : state.monitors) {
        if (state.selection.Contains(monitor.id)) {
            targets.push_back(monitor);
        }
    }
    return targets;
}

std::vector<std::wstring> BrightnessController::MonitorIds(const std::vector<MonitorInfo>& monitors) {
    std::vector<std::wstring> ids;
    ids.reserve(monitors.size());
    for (const MonitorInfo& monitor : monitors) {
        ids.push_back(monitor.id);
    }
    return ids;
}
