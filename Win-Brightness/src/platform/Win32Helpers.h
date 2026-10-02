#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

class RegistryKey {
public:
    RegistryKey() = default;
    explicit RegistryKey(HKEY key);
    ~RegistryKey();

    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;

    RegistryKey(RegistryKey&& other) noexcept;
    RegistryKey& operator=(RegistryKey&& other) noexcept;

    HKEY Get() const { return m_key; }
    HKEY* Put();
    explicit operator bool() const { return m_key != nullptr; }
    void Reset(HKEY key = nullptr);

private:
    HKEY m_key = nullptr;
};

class MemoryPaintDc {
public:
    MemoryPaintDc(HDC targetDc, int width, int height);
    ~MemoryPaintDc();

    MemoryPaintDc(const MemoryPaintDc&) = delete;
    MemoryPaintDc& operator=(const MemoryPaintDc&) = delete;

    HDC Get() const {
        return m_memoryDc && m_bitmap && m_oldBitmap && m_oldBitmap != HGDI_ERROR
            ? m_memoryDc : nullptr;
    }

private:
    HDC m_memoryDc = nullptr;
    HBITMAP m_bitmap = nullptr;
    HGDIOBJ m_oldBitmap = nullptr;
};

int GetDpiForPoint(POINT pt);
int GetDpiForHwnd(HWND hWnd);
int ScaleByDpi(int value, int dpi);
