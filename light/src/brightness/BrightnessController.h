#pragma once
#include "BrightnessTypes.h"
#include "HardwareBrightness.h"
#include "SoftwareBrightness.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

class BrightnessController {
public:
    BrightnessController();
    ~BrightnessController();

    bool Init();
    void Cleanup();

    int GetBrightness() const;
    bool SetBrightness(int percent);

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    void SetBrightnessMode(BrightnessMode mode);
    BrightnessMode GetBrightnessMode() const;

    void RefreshMonitors();
    bool IsHardwareAvailable() const;

private:
    void WorkerThreadProc();
    void ApplyBrightness(int level, BrightnessMode mode);
    int ReadBrightnessInternal();

    std::atomic<int> m_currentBrightness{50};
    std::atomic<int> m_brightnessMode{static_cast<int>(BrightnessMode::Hardware)};
    std::atomic<int> m_targetBrightness{-1};
    std::atomic<bool> m_brightnessEnabled{true};
    std::atomic<bool> m_stopWorker{false};
    std::atomic<bool> m_initialized{false};

    std::thread m_workerThread;
    std::mutex m_workerMutex;
    std::condition_variable m_workerCv;

    HardwareBrightness m_hardware;
    SoftwareBrightness m_software;
};
