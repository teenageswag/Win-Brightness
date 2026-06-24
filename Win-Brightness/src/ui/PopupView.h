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
    static constexpr int kBaseHeight = 40;
    static constexpr int kBasePaddingH = 14;
    static constexpr int kBasePaddingV = 8;

    static constexpr int kBaseToggleW = 28;
    static constexpr int kBaseToggleH = 14;
    static constexpr int kBaseToggleThumb = 10;

    static constexpr int kBaseToggleTrackGap = 10;

    static constexpr int kBaseTrackH = 4;
    static constexpr int kBaseThumbRadius = 5;

    static constexpr int kBaseTrackLabelGap = 8;

    static constexpr int kBaseLabelW = 38;

    static constexpr int kBaseWidth = 280;

    static constexpr UINT_PTR kAutoHideTimerId = 1;
    static constexpr UINT_PTR kDebounceTimerId = 2;
    static constexpr DWORD kAutoHideDelayMs = 5555;
    static constexpr DWORD kDebounceDelayMs = 80;

    struct Layout {
        int centerY = 0;

        RECT toggle{};

        int trackLeft = 0;
        int trackRight = 0;
        int trackCenterY = 0;
        int trackH = 0;

        Gdiplus::RectF labelRect{};

        void Compute(const RECT& client, int dpi);
    };

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

    Layout BuildLayout(int dpi) const;

    int XToPercent(int x, const Layout& layout) const;
};