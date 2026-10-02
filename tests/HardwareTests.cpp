#include "brightness/HardwareBrightness.h"
#include "FakeMonitorApi.h"
#include <cstdio>
#include <stdexcept>
#include <cstdlib>
#include <new>
#include <utility>

namespace { bool failNextAllocation = false; }

void* operator new(std::size_t size) {
    if (std::exchange(failNextAllocation, false)) throw std::bad_alloc();
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() try {
    HardwareBrightness hardware;
    std::vector<MonitorInfo> monitors(1);
    monitors[0].id = L"display";
    monitors[0].handle = reinterpret_cast<HMONITOR>(1);
    hardware.RefreshMonitors(monitors);
    Check(monitors[0].hardwareBrightness, "detect supported brightness");

    fake::writeFailureMask = 2;
    auto results = hardware.ApplyBrightness(35, {L"display"});
    Check(results.size() == 2, "return one result per physical monitor");
    Check(results[0].error == ERROR_SUCCESS && results[1].error == ERROR_GEN_FAILURE,
          "preserve partial failure");
    Check(results[0].physicalIndex == 0 && results[1].physicalIndex == 1 &&
          results[0].requestedValue == 35, "identify failed physical monitor and requested value");

    hardware.ApplyBrightness(35, {L"display"});
    Check(fake::writes.size() == 3 && fake::writes.back().handle == 2,
          "skip successful duplicate write and retry failed write");
    fake::writeFailureMask = 0;
    hardware.ApplyBrightness(35, {L"display"});
    const size_t afterRecovery = fake::writes.size();
    hardware.ApplyBrightness(35, {L"display"});
    Check(fake::writes.size() == afterRecovery, "cache only successfully written values");

    results = hardware.ApplyBrightness(35, {L"missing"});
    Check(results.size() == 1 && results[0].error == ERROR_NOT_SUPPORTED,
          "report missing physical target");

    fake::probeFailureMask = 2;
    monitors[0].hardwareError = ERROR_SUCCESS;
    hardware.RefreshMonitors(monitors);
    Check(monitors[0].hardwareError == ERROR_GEN_FAILURE,
          "capture discovery error before releasing handle");
    Check(monitors[0].hardwareStatus == HardwareStatus::Failed,
          "distinguish discovery failure from unsupported brightness");
    const size_t beforeRefreshWrite = fake::writes.size();
    results = hardware.ApplyBrightness(35, {L"display"});
    Check(fake::writes.size() == beforeRefreshWrite + 1, "invalidate write cache when handles refresh");
    Check(results.size() == 2 && results[1].physicalIndex == 1 && results[1].error == ERROR_GEN_FAILURE,
          "preserve rejected physical endpoint and error during writes");
    hardware.ReleaseMonitors();
    Check(fake::destroyed == 4, "release supported and rejected physical monitors exactly once");

    fake::probeFailureMask = 3;
    fake::probeError = ERROR_NOT_SUPPORTED;
    hardware.RefreshMonitors(monitors);
    Check(monitors[0].hardwareStatus == HardwareStatus::Unsupported && !monitors[0].hardwareBrightness,
          "recognize explicitly unsupported brightness");

    fake::probeFailureMask = 0;
    const unsigned beforeFailure = fake::destroyed;
    fake::afterAcquisition = [] { failNextAllocation = true; };
    bool failed = false;
    try {
        hardware.RefreshMonitors(monitors);
    } catch (const std::bad_alloc&) {
        failed = true;
    }
    fake::afterAcquisition = nullptr;
    Check(failed && fake::destroyed == beforeFailure + 2,
          "release every acquired handle when a cache allocation fails");

    hardware.RefreshMonitors(monitors);
    const unsigned beforeRelease = fake::destroyed;
    failNextAllocation = true;
    hardware.ReleaseMonitors();
    const bool noAllocation = failNextAllocation;
    failNextAllocation = false;
    Check(noAllocation && fake::destroyed == beforeRelease + 2,
          "release physical handles without allocating memory");
    std::puts("HardwareTests passed");
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
