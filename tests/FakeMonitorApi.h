#pragma once
#include <windows.h>
#include <physicalmonitorenumerationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace fake {
inline std::atomic<unsigned> writeFailureMask{0};
inline std::atomic<unsigned> probeFailureMask{0};
inline std::atomic<int> queryDelayMs{0};
inline std::atomic<int> writeDelayMs{0};
inline std::atomic<unsigned> destroyed{0};
inline std::atomic<DWORD> probeThread{0};
inline std::mutex writesMutex;
struct Write { uintptr_t handle; DWORD value; };
inline std::vector<Write> writes;
inline bool Fails(unsigned mask, HANDLE handle) {
    return (mask & (1u << (reinterpret_cast<uintptr_t>(handle) - 1))) != 0;
}
}

extern "C" {
BOOL WINAPI GetNumberOfPhysicalMonitorsFromHMONITOR(HMONITOR, LPDWORD count) {
    *count = 2;
    return TRUE;
}
BOOL WINAPI GetPhysicalMonitorsFromHMONITOR(HMONITOR, DWORD count, LPPHYSICAL_MONITOR monitors) {
    if (count != 2) return FALSE;
    for (DWORD i = 0; i < count; ++i) {
        monitors[i] = {};
        monitors[i].hPhysicalMonitor = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(i + 1));
    }
    return TRUE;
}
BOOL WINAPI GetVCPFeatureAndVCPFeatureReply(HANDLE handle, BYTE, LPMC_VCP_CODE_TYPE type,
                                          LPDWORD current, LPDWORD maximum) {
    fake::probeThread = GetCurrentThreadId();
    std::this_thread::sleep_for(std::chrono::milliseconds(fake::queryDelayMs.load()));
    if (fake::Fails(fake::probeFailureMask.load(), handle)) {
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }
    if (type) *type = MC_SET_PARAMETER;
    *current = 50;
    *maximum = 100;
    return TRUE;
}
BOOL WINAPI SetVCPFeature(HANDLE handle, BYTE, DWORD value) {
    {
        std::lock_guard lock(fake::writesMutex);
        fake::writes.push_back({reinterpret_cast<uintptr_t>(handle), value});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(fake::writeDelayMs.load()));
    if (fake::Fails(fake::writeFailureMask.load(), handle)) {
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }
    return TRUE;
}
BOOL WINAPI DestroyPhysicalMonitor(HANDLE) {
    ++fake::destroyed;
    SetLastError(ERROR_SUCCESS); // Detect error capture that happens too late.
    return TRUE;
}
BOOL WINAPI DestroyPhysicalMonitors(DWORD count, LPPHYSICAL_MONITOR monitors) {
    for (DWORD i = 0; i < count; ++i) DestroyPhysicalMonitor(monitors[i].hPhysicalMonitor);
    return TRUE;
}
}
