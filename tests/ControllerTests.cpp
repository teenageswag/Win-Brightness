#include "brightness/BrightnessController.h"
#include "brightness/MonitorCatalog.h"
#include "FakeMonitorApi.h"
#include <cstdio>
#include <stdexcept>

namespace {
std::atomic<unsigned> enumerations{0};
std::atomic<bool> failEnumeration{false};
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Predicate>
void WaitUntil(Predicate predicate, const char* message, int timeoutMs = 3000) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!predicate() && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(predicate(), message);
}
struct Window {
    HWND handle = CreateWindowW(L"STATIC", L"", WS_POPUP, 0, 0, 1, 1,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Window() { if (handle) DestroyWindow(handle); }
};
}

std::expected<std::vector<MonitorInfo>, DWORD> MonitorCatalog::Enumerate() {
    ++enumerations;
    if (failEnumeration) return std::unexpected(ERROR_GEN_FAILURE);
    std::vector<MonitorInfo> monitors(1);
    monitors[0].id = L"display";
    monitors[0].handle = reinterpret_cast<HMONITOR>(1);
    return monitors;
}

int main() try {
    Window window;
    Check(window.handle != nullptr, "create notification window");
    BrightnessController controller;
    fake::queryDelayMs = 200;
    const auto start = std::chrono::steady_clock::now();
    Check(controller.Init(window.handle), "start controller");
    Check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(150),
          "initialization does not wait for VCP probes");
    WaitUntil([&] { return !controller.GetMonitors().empty(); }, "publish asynchronous catalog");
    Check(fake::probeThread != GetCurrentThreadId(), "probe DDC on background thread");
    fake::queryDelayMs = 0;

    fake::probeFailureMask = 3;
    controller.RequestMonitorRefresh();
    WaitUntil([&] { return controller.GetMonitors()[0].hardwareStatus == HardwareStatus::Failed; },
              "publish transient failure");
    fake::probeFailureMask = 0;
    WaitUntil([&] { return controller.GetMonitors()[0].hardwareStatus == HardwareStatus::Available; },
              "recover transient failure without another UI event");

    failEnumeration = true;
    controller.RequestMonitorRefresh();
    WaitUntil([&] { return controller.GetCatalogError() != ERROR_SUCCESS; }, "report catalog failure");
    Check(controller.GetMonitors().size() == 1, "preserve last complete catalog on enumeration failure");
    failEnumeration = false;
    WaitUntil([&] { return controller.GetCatalogError() == ERROR_SUCCESS; }, "retry catalog failure");

    fake::probeError = ERROR_NOT_SUPPORTED;
    fake::probeFailureMask = 3;
    controller.RequestMonitorRefresh();
    WaitUntil([&] { return controller.GetMonitors()[0].hardwareStatus == HardwareStatus::Unsupported; },
              "report explicitly unsupported endpoint");
    const unsigned before = enumerations;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    Check(enumerations == before, "do not retry unsupported endpoints");

    fake::probeError = ERROR_GEN_FAILURE;
    controller.RequestMonitorRefresh();
    WaitUntil([&] { return enumerations >= before + 4; }, "run three bounded retries", 6000);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    Check(enumerations == before + 4, "stop retrying after three retries");
    controller.Cleanup();

    fake::probeFailureMask = 0;
    fake::writeFailureMask = 0;
    fake::writeDelayMs = 200;
    controller.SetBrightnessMode(BrightnessMode::Hardware);
    {
        std::lock_guard lock(fake::writesMutex);
        fake::writes.clear();
    }
    Check(controller.Init(window.handle), "restart controller for shutdown test");
    WaitUntil([] {
        std::lock_guard lock(fake::writesMutex);
        return !fake::writes.empty();
    }, "start first slow write");
    controller.Cleanup();
    {
        std::lock_guard lock(fake::writesMutex);
        Check(fake::writes.size() == 1, "stop before second physical monitor write");
    }
    std::puts("ControllerTests passed");
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
