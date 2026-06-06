#include "BrightnessController.h"

BrightnessController::BrightnessController() = default;

BrightnessController::~BrightnessController() {
    Cleanup();
}

bool BrightnessController::Init() {
    if (m_initialized.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }

    RefreshMonitors();
    m_currentBrightness.store(ReadBrightnessInternal(), std::memory_order_relaxed);

    m_stopWorker.store(false, std::memory_order_release);
    m_workerThread = std::thread(&BrightnessController::WorkerThreadProc, this);
    return true;
}

void BrightnessController::Cleanup() {
    if (!m_initialized.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    m_stopWorker.store(true, std::memory_order_release);
    m_workerCv.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    m_software.Reset();
    m_hardware.ReleaseMonitors();
}

int BrightnessController::GetBrightness() const { return m_currentBrightness.load(std::memory_order_relaxed); }

bool BrightnessController::SetBrightness(int percent) {
    const int clamped = ClampBrightness(percent);
    m_currentBrightness.store(clamped, std::memory_order_relaxed);
    if (IsEnabled()) {
        m_targetBrightness.store(clamped, std::memory_order_release);
        m_workerCv.notify_one();
    }
    return true;
}

void BrightnessController::SetEnabled(bool enabled) {
    const bool wasEnabled = m_enabled.exchange(enabled, std::memory_order_acq_rel);
    if (wasEnabled == enabled) {
        return;
    }

    if (enabled) {
        SetBrightness(GetBrightness());
    } else {
        m_targetBrightness.store(-1, std::memory_order_release);
        m_software.Reset();
    }
}

bool BrightnessController::IsEnabled() const {
    return m_enabled.load(std::memory_order_acquire);
}

void BrightnessController::SetBrightnessMode(BrightnessMode mode) {
    const BrightnessMode nextMode = (mode == BrightnessMode::Software || mode == BrightnessMode::Hardware) ? mode : BrightnessMode::Hardware;

    m_brightnessMode.store(static_cast<int>(nextMode), std::memory_order_release);
    if (IsEnabled()) {
        SetBrightness(GetBrightness());
    } else {
        m_software.Reset();
    }
}

BrightnessMode BrightnessController::GetBrightnessMode() const {
    const int value = m_brightnessMode.load(std::memory_order_acquire);
    return value == static_cast<int>(BrightnessMode::Software) ? BrightnessMode::Software : BrightnessMode::Hardware;
}

void BrightnessController::RefreshMonitors() {
    m_hardware.RefreshMonitors();
}

bool BrightnessController::IsHardwareAvailable() const {
    return m_hardware.IsAvailable();
}

void BrightnessController::WorkerThreadProc() {
    int lastApplied = -1;
    int lastAppliedMode = -1;

    while (true) {
        int target = -1;
        bool forceApply = false;
        {
            std::unique_lock<std::mutex> lock(m_workerMutex);
            m_workerCv.wait(lock, [this] {
                return m_stopWorker.load(std::memory_order_acquire) ||
                       m_forceApply.load(std::memory_order_acquire) ||
                       m_targetBrightness.load(std::memory_order_acquire) != -1;
            });

            if (m_stopWorker.load(std::memory_order_acquire)) {
                break;
            }

            forceApply = m_forceApply.exchange(false, std::memory_order_acq_rel);
            target = m_targetBrightness.exchange(-1, std::memory_order_acq_rel);
        }

        if (m_forceApply.exchange(false, std::memory_order_acq_rel)) {
            forceApply = true;
        }

        const int latest = m_targetBrightness.exchange(-1, std::memory_order_acq_rel);
        if (latest != -1) {
            target = latest;
        }

        if (target == -1 && forceApply) {
            target = GetBrightness();
        }

        if (target == -1) {
            continue;
        }

        const BrightnessMode mode = GetBrightnessMode();
        const int modeValue = static_cast<int>(mode);
        if (!forceApply && target == lastApplied && modeValue == lastAppliedMode) {
            continue;
        }

        lastApplied = target;
        lastAppliedMode = modeValue;
        ApplyBrightness(target, mode);
    }

    m_software.Reset();
}

void BrightnessController::ApplyBrightness(int level, BrightnessMode mode) {
    if (!IsEnabled()) {
        m_software.Reset();
        return;
    }

    const int clamped = ClampBrightness(level);

    if (mode == BrightnessMode::Hardware && m_hardware.ApplyBrightness(clamped)) {
        m_software.Reset();
        return;
    }

    m_software.ApplyBrightness(clamped);
}

int BrightnessController::ReadBrightnessInternal() {
    const int ddcBrightness = m_hardware.ReadBrightness();
    if (ddcBrightness >= kMinBrightness && ddcBrightness <= kMaxBrightness) {
        return ddcBrightness;
    }

    return m_software.ReadBrightness();
}
