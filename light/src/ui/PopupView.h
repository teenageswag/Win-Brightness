#pragma once
#include "../brightness/BrightnessController.h"
#include <gdiplus.h>
#include <windows.h>

class PopupView {
public:
    PopupView(HINSTANCE hInstance, BrightnessController& controller);
    ~PopupView();

    bool Register();
    bool Create();
    void Toggle(POINT cursorPt);
    void Hide();
    bool IsVisible() const;
    HWND GetHWnd() const { return m_hWnd; }

    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_isEnabled; }
    void UpdateFromController();

    LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    static constexpr int kBaseWidth = 280;
    static constexpr int kBaseHeight = 40;
    static constexpr int kPadding = 10;
    static constexpr int kTrackHeight = 4;
    static constexpr int kThumbRadius = 5;
    static constexpr int kPercentWidth = 44;
    static constexpr int kTrackPercentGap = 8;

    // Toggle switch dimensions
    static constexpr int kToggleWidth = 28;
    static constexpr int kToggleHeight = 14;
    static constexpr int kToggleThumbSize = 10;
    static constexpr int kToggleLeftMargin = 10;
    static constexpr int kToggleTrackGap = 10;

    static constexpr UINT_PTR kAutoHideTimerId = 1;
    static constexpr UINT_PTR kDebounceTimerId = 2;
    static constexpr DWORD kAutoHideDelayMs = 5555;
    static constexpr DWORD kDebounceDelayMs = 80;

    HINSTANCE m_hInstance;
    HWND m_hWnd = nullptr;
    BrightnessController& m_controller;
    bool m_isDragging = false;
    ULONGLONG m_showTime = 0;
    int m_displayBrightness = 50;
    bool m_hasPendingBrightness = false;

    bool m_isEnabled = true;
    int m_savedBrightness = 50;

    void ResetAutoHideTimer();
    void ResetDebounceTimer();
    void CommitPendingBrightness();
    void SetDisplayedBrightness(int percent);
    void NotifyOwnerBrightnessChanged(int percent);
    void NotifyOwnerEnabledChanged(bool enabled);

    void GetToggleBounds(int dpi, RECT& rect) const;
    void GetTrackBounds(int dpi, int& left, int& right, int& centerY) const;
    int XToPercent(int x, int dpi) const;
};
