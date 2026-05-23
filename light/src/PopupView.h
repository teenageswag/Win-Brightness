#pragma once

#include <windows.h>
#include <gdiplus.h>
#include "BrightnessController.h"

class PopupView
{
public:
    PopupView(HINSTANCE hInstance, BrightnessController &controller);
    ~PopupView();

    bool Register();
    bool Create();
    void Toggle(POINT cursorPt);
    void Hide();
    bool IsVisible() const;
    HWND GetHWnd() const { return m_hWnd; }

    LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    static constexpr int kBaseWidth = 220;
    static constexpr int kBaseHeight = 38;
    static constexpr int kPadding = 8;
    static constexpr int kTrackHeight = 4;
    static constexpr int kThumbRadius = 5;
    static constexpr int kPercentWidth = 42;

    static constexpr UINT_PTR kAutoHideTimerId = 1;
    static constexpr UINT_PTR kDebounceTimerId = 2;
    static constexpr DWORD kAutoHideDelayMs = 3000;
    static constexpr DWORD kDebounceDelayMs = 80;

    HINSTANCE m_hInstance;
    HWND m_hWnd = nullptr;
    BrightnessController &m_controller;
    bool m_isDragging = false;
    ULONGLONG m_showTime = 0;
    int m_displayBrightness = 50;
    bool m_hasPendingBrightness = false;

    void ResetAutoHideTimer();
    void ResetDebounceTimer();
    void CommitPendingBrightness();
    void SetDisplayedBrightness(int percent);
    void NotifyOwnerBrightnessChanged(int percent);

    int GetWindowDpi() const;
    int GetPointDpi(POINT pt) const;
    int Scale(int value, int dpi) const;

    void GetTrackBounds(int dpi, int &left, int &right, int &centerY) const;
    int XToPercent(int x, int dpi) const;
};
