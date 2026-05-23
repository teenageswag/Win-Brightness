#include "BrightnessController.h"

#include <algorithm>
#include <cmath>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>

#pragma comment(lib, "dxva2.lib")

namespace
{

    static constexpr double kGammaCurve = 1.0;
    static constexpr double kMinGammaFactor = 0.002;
    static constexpr const wchar_t *kDimOverlayClassName = L"BrightnessDimOverlay";
    static constexpr double kOverlayCurve = 0.85;
    static constexpr int kMaxOverlayAlpha = 245;

    std::vector<HWND> g_overlayWindows;

    class ScreenDc
    {
    public:
        ScreenDc() : hdc_(GetDC(nullptr)) {}
        ~ScreenDc()
        {
            if (hdc_)
            {
                ReleaseDC(nullptr, hdc_);
            }
        }

        ScreenDc(const ScreenDc &) = delete;
        ScreenDc &operator=(const ScreenDc &) = delete;

        HDC Get() const { return hdc_; }

    private:
        HDC hdc_ = nullptr;
    };

    LRESULT CALLBACK DimOverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_ERASEBKGND:
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(reinterpret_cast<HDC>(wParam), &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            return 1;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    void RegisterDimOverlayClass()
    {
        static bool registered = false;
        if (registered)
        {
            return;
        }

        WNDCLASSEX wcex = {sizeof(wcex)};
        wcex.lpfnWndProc = DimOverlayWndProc;
        wcex.hInstance = GetModuleHandle(nullptr);
        wcex.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wcex.lpszClassName = kDimOverlayClassName;
        registered = RegisterClassEx(&wcex) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    BYTE OverlayAlphaFromPercent(int percent)
    {
        if (percent >= 100)
        {
            return 0;
        }

        percent = std::max(percent, 1);
        const double dim = (100.0 - percent) / 99.0;
        const double shaped = std::pow(dim, kOverlayCurve);
        const int alpha = static_cast<int>(kMaxOverlayAlpha * shaped + 0.5);
        return static_cast<BYTE>(std::clamp(alpha, 0, kMaxOverlayAlpha));
    }

    std::vector<RECT> GetMonitorRects()
    {
        std::vector<RECT> rects;
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMonitor, HDC, LPRECT, LPARAM data) -> BOOL
                            {
        auto* monitorRects = reinterpret_cast<std::vector<RECT>*>(data);
        MONITORINFO monitorInfo = { sizeof(monitorInfo) };
        if (GetMonitorInfo(hMonitor, &monitorInfo)) {
            monitorRects->push_back(monitorInfo.rcMonitor);
        }
        return TRUE; }, reinterpret_cast<LPARAM>(&rects));
        return rects;
    }

    void DestroyOverlayWindows()
    {
        for (HWND hWnd : g_overlayWindows)
        {
            if (IsWindow(hWnd))
            {
                DestroyWindow(hWnd);
            }
        }
        g_overlayWindows.clear();

        while (HWND hWnd = FindWindow(kDimOverlayClassName, nullptr))
        {
            if (!DestroyWindow(hWnd))
            {
                break;
            }
        }
    }

} // namespace

BrightnessController::BrightnessController() = default;

BrightnessController::~BrightnessController()
{
    Cleanup();
}

bool BrightnessController::Init()
{
    RefreshMonitors();
    m_currentBrightness.store(ReadBrightnessInternal(), std::memory_order_relaxed);

    m_stopWorker.store(false, std::memory_order_release);
    m_workerThread = std::thread(&BrightnessController::WorkerThreadProc, this);
    return true;
}

void BrightnessController::Cleanup()
{
    m_stopWorker.store(true, std::memory_order_release);
    m_workerCv.notify_all();
    if (m_workerThread.joinable())
    {
        m_workerThread.join();
    }

    ApplyGamma(kMaxBrightness);
    DestroyOverlayWindows();
    ReleaseCachedMonitors();
}

int BrightnessController::GetBrightness() const
{
    return m_currentBrightness.load(std::memory_order_relaxed);
}

bool BrightnessController::SetBrightness(int percent)
{
    const int clamped = ClampBrightness(percent);
    m_currentBrightness.store(clamped, std::memory_order_relaxed);
    m_targetBrightness.store(clamped, std::memory_order_release);
    m_workerCv.notify_one();
    return true;
}

void BrightnessController::SetBrightnessMode(BrightnessMode mode)
{
    const int value = static_cast<int>(mode);
    if (mode != BrightnessMode::Hardware && mode != BrightnessMode::Software)
    {
        m_brightnessMode.store(static_cast<int>(BrightnessMode::Hardware), std::memory_order_release);
    }
    else
    {
        m_brightnessMode.store(value, std::memory_order_release);
    }
    SetBrightness(GetBrightness());
}

BrightnessMode BrightnessController::GetBrightnessMode() const
{
    const int value = m_brightnessMode.load(std::memory_order_acquire);
    return value == static_cast<int>(BrightnessMode::Software)
               ? BrightnessMode::Software
               : BrightnessMode::Hardware;
}

void BrightnessController::RefreshMonitors()
{
    std::vector<CachedPhysicalMonitor> monitors;

    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMonitor, HDC, LPRECT, LPARAM data) -> BOOL
                        {
        auto* cachedMonitors = reinterpret_cast<std::vector<CachedPhysicalMonitor>*>(data);

        DWORD monitorCount = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &monitorCount) || monitorCount == 0) {
            return TRUE;
        }

        std::vector<PHYSICAL_MONITOR> physicalMonitors(monitorCount);
        if (!GetPhysicalMonitorsFromHMONITOR(hMonitor, monitorCount, physicalMonitors.data())) {
            return TRUE;
        }

        for (DWORD i = 0; i < monitorCount; ++i) {
            DWORD minBrightness = 0;
            DWORD currentBrightness = 0;
            DWORD maxBrightness = 0;
            if (GetMonitorBrightness(physicalMonitors[i].hPhysicalMonitor,
                                     &minBrightness,
                                     &currentBrightness,
                                     &maxBrightness) &&
                maxBrightness > minBrightness) {
                CachedPhysicalMonitor cachedMonitor;
                cachedMonitor.monitor = physicalMonitors[i];
                cachedMonitor.minBrightness = minBrightness;
                cachedMonitor.maxBrightness = maxBrightness;
                cachedMonitors->push_back(cachedMonitor);
            } else {
                DestroyPhysicalMonitor(physicalMonitors[i].hPhysicalMonitor);
            }
        }

        return TRUE; }, reinterpret_cast<LPARAM>(&monitors));

    std::lock_guard<std::mutex> lock(m_monitorMutex);
    ReleaseCachedMonitors();
    m_physicalMonitors = std::move(monitors);
}

bool BrightnessController::IsHardwareAvailable() const
{
    std::lock_guard<std::mutex> lock(m_monitorMutex);
    return !m_physicalMonitors.empty();
}

int BrightnessController::ClampBrightness(int percent)
{
    return std::clamp(percent, kMinBrightness, kMaxBrightness);
}

void BrightnessController::WorkerThreadProc()
{
    int lastApplied = -1;

    while (true)
    {
        int target = -1;
        {
            std::unique_lock<std::mutex> lock(m_workerMutex);
            m_workerCv.wait(lock, [this]
                            { return m_stopWorker.load(std::memory_order_acquire) ||
                                     m_targetBrightness.load(std::memory_order_acquire) != -1; });

            if (m_stopWorker.load(std::memory_order_acquire))
            {
                break;
            }

            target = m_targetBrightness.exchange(-1, std::memory_order_acq_rel);
        }

        const int latest = m_targetBrightness.exchange(-1, std::memory_order_acq_rel);
        if (latest != -1)
        {
            target = latest;
        }

        if (target == -1 || target == lastApplied)
        {
            continue;
        }

        lastApplied = target;
        ApplyBrightness(target, GetBrightnessMode());
    }

    ApplyGamma(kMaxBrightness);
    DestroyOverlayWindows();
}

void BrightnessController::ApplyBrightness(int level, BrightnessMode mode)
{
    const int clamped = ClampBrightness(level);

    if (mode == BrightnessMode::Hardware && ApplyDdc(clamped))
    {
        ApplyGamma(kMaxBrightness);
        DestroyOverlayWindows();
        return;
    }

    ApplyGamma(clamped);
    ApplySoftwareOverlay(clamped);
}

int BrightnessController::ReadBrightnessInternal()
{
    const int ddcBrightness = ReadBrightnessDdc();
    if (ddcBrightness >= kMinBrightness && ddcBrightness <= kMaxBrightness)
    {
        return ddcBrightness;
    }

    return ReadBrightnessGamma();
}

int BrightnessController::ReadBrightnessDdc()
{
    std::lock_guard<std::mutex> lock(m_monitorMutex);
    for (const CachedPhysicalMonitor &cachedMonitor : m_physicalMonitors)
    {
        DWORD minBrightness = 0;
        DWORD currentBrightness = 0;
        DWORD maxBrightness = 0;
        if (GetMonitorBrightness(cachedMonitor.monitor.hPhysicalMonitor,
                                 &minBrightness,
                                 &currentBrightness,
                                 &maxBrightness) &&
            maxBrightness > minBrightness)
        {
            const int percent = static_cast<int>(
                (currentBrightness - minBrightness) * 100.0 / (maxBrightness - minBrightness) + 0.5);
            return ClampBrightness(percent);
        }
    }

    return -1;
}

int BrightnessController::ReadBrightnessGamma()
{
    WORD ramp[3][256];
    ScreenDc screenDc;
    if (!screenDc.Get())
    {
        return 50;
    }

    if (!GetDeviceGammaRamp(screenDc.Get(), ramp))
    {
        return 50;
    }

    const int percent = static_cast<int>(ramp[0][255] * 100.0 / 65535.0 + 0.5);
    return ClampBrightness(percent);
}

bool BrightnessController::ApplyDdc(int percent)
{
    bool applied = false;
    std::lock_guard<std::mutex> lock(m_monitorMutex);

    for (const CachedPhysicalMonitor &cachedMonitor : m_physicalMonitors)
    {
        const DWORD target = cachedMonitor.minBrightness + static_cast<DWORD>(
                                                               (cachedMonitor.maxBrightness - cachedMonitor.minBrightness) * (percent / 100.0) + 0.5);
        if (SetMonitorBrightness(cachedMonitor.monitor.hPhysicalMonitor, target))
        {
            applied = true;
        }
    }

    return applied;
}

void BrightnessController::ApplyGamma(int percent)
{
    const int clamped = ClampBrightness(percent);
    double factor = std::pow(clamped / 100.0, kGammaCurve);
    factor = std::max(factor, kMinGammaFactor);

    WORD ramp[3][256];
    for (int i = 0; i < 256; ++i)
    {
        const double scaled = std::min((i / 255.0) * factor, 1.0);
        const WORD value = static_cast<WORD>(scaled * 65535.0 + 0.5);
        ramp[0][i] = ramp[1][i] = ramp[2][i] = value;
    }

    ScreenDc screenDc;
    if (screenDc.Get())
    {
        SetDeviceGammaRamp(screenDc.Get(), ramp);
    }
}

void BrightnessController::ApplySoftwareOverlay(int percent)
{
    const int clamped = ClampBrightness(percent);
    const BYTE alpha = OverlayAlphaFromPercent(clamped);
    if (alpha == 0)
    {
        DestroyOverlayWindows();
        return;
    }

    RegisterDimOverlayClass();
    const std::vector<RECT> monitorRects = GetMonitorRects();
    if (monitorRects.empty())
    {
        return;
    }

    while (g_overlayWindows.size() > monitorRects.size())
    {
        HWND hWnd = g_overlayWindows.back();
        g_overlayWindows.pop_back();
        if (IsWindow(hWnd))
        {
            DestroyWindow(hWnd);
        }
    }

    HINSTANCE hInstance = GetModuleHandle(nullptr);
    for (size_t i = 0; i < monitorRects.size(); ++i)
    {
        const RECT &rect = monitorRects[i];
        HWND hWnd = (i < g_overlayWindows.size()) ? g_overlayWindows[i] : nullptr;

        if (!IsWindow(hWnd))
        {
            hWnd = CreateWindowEx(
                WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                kDimOverlayClassName,
                L"",
                WS_POPUP,
                rect.left,
                rect.top,
                rect.right - rect.left,
                rect.bottom - rect.top,
                nullptr,
                nullptr,
                hInstance,
                nullptr);

            if (i < g_overlayWindows.size())
            {
                g_overlayWindows[i] = hWnd;
            }
            else
            {
                g_overlayWindows.push_back(hWnd);
            }
        }

        if (!hWnd)
        {
            continue;
        }

        SetLayeredWindowAttributes(hWnd, 0, alpha, LWA_ALPHA);
        SetWindowPos(hWnd,
                     HWND_TOPMOST,
                     rect.left,
                     rect.top,
                     rect.right - rect.left,
                     rect.bottom - rect.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }
}

void BrightnessController::ReleaseCachedMonitors()
{
    if (!m_physicalMonitors.empty())
    {
        std::vector<PHYSICAL_MONITOR> monitors;
        monitors.reserve(m_physicalMonitors.size());
        for (const CachedPhysicalMonitor &cachedMonitor : m_physicalMonitors)
        {
            monitors.push_back(cachedMonitor.monitor);
        }
        DestroyPhysicalMonitors(static_cast<DWORD>(monitors.size()), monitors.data());
        m_physicalMonitors.clear();
    }
}
