#pragma once

#include "../brightness/BrightnessTypes.h"
#include <functional>
#include <gdiplus.h>
#include <vector>
#include <windows.h>

struct PopupState {
    int brightness = kDefaultBrightness;
    bool enabled = true;
    BrightnessMode mode = BrightnessMode::Software;
    MonitorSelection selection;
    std::vector<MonitorInfo> monitors;
    bool autostart = false;
    bool hotkeyAvailable = true;
    DWORD catalogError = ERROR_SUCCESS;
    LSTATUS settingsError = ERROR_SUCCESS;
    LSTATUS autostartError = ERROR_SUCCESS;
};

struct PopupActions {
    std::function<void(int)> setBrightness;
    std::function<void(bool)> setEnabled;
    std::function<void(BrightnessMode)> setMode;
    std::function<void(MonitorSelection)> setSelection;
    std::function<void(bool)> setAutostart;
};

class PopupView {
public:
    PopupView(HINSTANCE hInstance, PopupActions actions);
    ~PopupView();

    bool Register();
    bool Create();
    void Toggle(POINT monitorPoint, bool keyboardInvoked = false);
    void Hide(bool animated = false);
    bool IsVisible() const;
    HWND GetHWnd() const { return m_hWnd; }

    void SetState(PopupState state);
    LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    static constexpr int kBaseWidth = 620;
    static constexpr int kBaseHeight = 600;
    static constexpr int kBaseMinimumHeight = 514;
    static constexpr UINT_PTR kBrightnessTimerId = 1;
    static constexpr UINT_PTR kAutoHideTimerId = 2;
    static constexpr DWORD kBrightnessDelayMs = 70;
    static constexpr DWORD kAutoHideDelayMs = 2000;
    static constexpr DWORD kFadeDurationMs = 170;

    enum class FocusKind {
        None,
        Power,
        Slider,
        SoftwareMode,
        HardwareMode,
        AllDisplays,
        SelectedDisplays,
        Monitor,
        Autostart
    };

    struct FocusTarget {
        FocusKind kind = FocusKind::None;
        size_t monitorIndex = 0;

        bool operator==(const FocusTarget&) const = default;
    };

    struct Layout {
        RECT power{};
        RECT brightnessCard{};
        RECT sliderHit{};
        RECT softwareMode{};
        RECT hardwareMode{};
        RECT allDisplays{};
        RECT selectedDisplays{};
        RECT monitorViewport{};
        RECT autostart{};
        std::vector<RECT> monitorItems;
        int sliderLeft = 0;
        int sliderRight = 0;
        int sliderY = 0;
        int monitorItemHeight = 0;
        int monitorItemGap = 0;

        void Compute(const RECT& client, int dpi, size_t monitorCount, int scrollOffset);
    };

    HINSTANCE m_hInstance = nullptr;
    HWND m_hWnd = nullptr;
    PopupActions m_actions;
    PopupState m_state;
    FocusTarget m_focus{FocusKind::Slider, 0};
    FocusTarget m_hover;
    FocusTarget m_pressed;
    bool m_showKeyboardFocus = false;
    bool m_isDragging = false;
    bool m_trackingMouse = false;
    bool m_hasPendingBrightness = false;
    int m_scrollOffset = 0;
    ULONGLONG m_showTime = 0;

    Layout BuildLayout() const;
    std::vector<FocusTarget> FocusOrder() const;
    FocusTarget HitTest(POINT point, const Layout& layout) const;
    RECT RectForTarget(const FocusTarget& target, const Layout& layout) const;

    void SetFocusTarget(FocusTarget target, bool keyboardFocus);
    void MoveFocus(bool backwards);
    void EnsureFocusedMonitorVisible();
    void Activate(const FocusTarget& target);
    void UpdateAccessibleName();

    int XToBrightness(int x, const Layout& layout) const;
    void SetDisplayedBrightness(int percent);
    void QueueBrightnessCommit();
    void CommitBrightness();
    void ResetAutoHideTimer();
    bool IsCursorOverWindow() const;
    void ScrollMonitors(int direction);
    int MaximumScroll(const Layout& layout) const;
};
