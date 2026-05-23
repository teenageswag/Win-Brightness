#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <physicalmonitorenumerationapi.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

enum class BrightnessMode
{
    Hardware = 0,
    Software = 1
};

class BrightnessController
{
public:
    BrightnessController();
    ~BrightnessController();

    bool Init();
    void Cleanup();

    int GetBrightness() const;
    bool SetBrightness(int percent);

    void SetBrightnessMode(BrightnessMode mode);
    BrightnessMode GetBrightnessMode() const;

    void RefreshMonitors();
    bool IsHardwareAvailable() const;

private:
    struct CachedPhysicalMonitor
    {
        PHYSICAL_MONITOR monitor{};
        DWORD minBrightness = 0;
        DWORD maxBrightness = 100;
    };

    static constexpr int kMinBrightness = 1;
    static constexpr int kMaxBrightness = 100;

    std::atomic<int> m_currentBrightness{50};
    std::atomic<int> m_brightnessMode{static_cast<int>(BrightnessMode::Hardware)};

    std::thread m_workerThread;
    std::mutex m_workerMutex;
    std::condition_variable m_workerCv;
    std::atomic<int> m_targetBrightness{-1};
    std::atomic<bool> m_stopWorker{false};

    mutable std::mutex m_monitorMutex;
    std::vector<CachedPhysicalMonitor> m_physicalMonitors;

    static int ClampBrightness(int percent);

    void WorkerThreadProc();
    void ApplyBrightness(int level, BrightnessMode mode);

    int ReadBrightnessInternal();
    int ReadBrightnessDdc();
    int ReadBrightnessGamma();

    bool ApplyDdc(int percent);
    static void ApplyGamma(int percent);
    static void ApplySoftwareOverlay(int percent);
    void ReleaseCachedMonitors();
};
