# trenches

A small Windows utility for dimming one display, a custom group, or every connected display.

![trenches dynamic island](image/island-dark.png)

## Features

- Per-display targeting with **All displays** and **Selected** scopes
- **Software** mode with independent click-through overlays
- **Hardware** mode using DDC/CI brightness control
- Dynamic island attached to the top center of the primary display, opened from the tray or with `Ctrl+Alt+B`
- Compact primary-display controls and an expanding monitor list with five visible rows and scrolling
- Interruptible spring transitions, continuous corners, and per-pixel transparency through DirectComposition
- Automatic light/dark theme, optional translucency, and appearance preferences in the tray
- Global pause/resume without losing the saved brightness value
- Optional launch at Windows sign-in
- Per-monitor DPI awareness v2, keyboard controls without foreground activation, and Windows High Contrast support
- Single-instance behavior: launching trenches again opens the existing control panel

Hardware mode is available only when a display exposes the DDC/CI brightness VCP feature. Explicitly unsupported displays are marked `No DDC/CI` and remain available in Software mode. Temporary detection or write failures are shown as `DDC/CI failed` with the Windows error code. The app retries temporary failures up to three times and checks displays again after resume or a display configuration change.

Pausing Hardware mode sets the selected displays to 100%. Switching to Software mode or excluding a display restores the hardware brightness changed by the app to 100% before enabling an overlay. Exiting leaves the last hardware brightness in place; software overlays are removed.

Successful duplicate hardware writes are skipped until the monitor catalog is refreshed. Changes made through the monitor's own controls are not polled.

The existing controller stores one brightness value for the selected group. Expanded sliders therefore show the same target for selected displays; unselected displays show a dash. Selecting a row toggles its membership, and dragging an unselected row selects that display. Compact brightness input targets the primary display. Opening or expanding the island alone does not change the selection. Percentages are optimistic requested values, not readbacks of each monitor's physical brightness.

Windows startup commands have a [260-character limit](https://learn.microsoft.com/en-us/windows/win32/setupapi/run-and-runonce-registry-keys). For a longer app path, trenches uses its short path when available; otherwise it reports that a shorter app path is needed.

## Controls

| Input | Action |
| --- | --- |
| `Ctrl+Alt+B` | Open or close the island on the primary display |
| Tray icon click | Open or close the island |
| Island chevron / `Ctrl+Alt+E` | Expand or collapse |
| `Ctrl+Alt+N` / `Ctrl+Alt+Shift+N` | Move between controls |
| `Ctrl+Alt` + arrows | Adjust brightness or move within grouped choices |
| `Ctrl+Alt+Page Up` / `Ctrl+Alt+Page Down` | Adjust brightness by 10 percentage points |
| `Ctrl+Alt+Home` / `Ctrl+Alt+End` | Set minimum or maximum brightness |
| `Ctrl+Alt+Enter` | Activate the highlighted control |
| `Ctrl+Alt+Escape` | Collapse; close when already compact |
| Mouse wheel | Scroll the expanded list; adjust primary brightness when compact |
| Tray right-click / island right-click | Open preferences and exit menu |

The window does not activate the application or steal keyboard focus. Temporary navigation shortcuts are registered when opened with `Ctrl+Alt+B` and released when closing. Shortcuts already owned by another application remain unavailable. The island stays open until explicitly closed. On narrow DPI-scaled desktops the footer uses two rows; a short screen can display fewer than five monitors.

Inter Regular, Medium, and SemiBold are embedded in the executable and loaded into a private DirectWrite collection. Segoe UI is the fallback. Distribution includes `Inter-LICENSE.txt`; no font installation is needed. Translucency blends with the desktop through alpha; it does not apply Acrylic/Mica background blur.

## Build

Requirements:

- Windows 10 or 11
- Visual Studio Build Tools with MSVC and the Windows SDK
- C++23 support

Open `Win-Brightness.slnx` in Visual Studio and build `Release | x64`, or run MSBuild directly. The executable is written to:

```text
build/Release/trenches.exe
build/Release/Inter-LICENSE.txt
```

## Architecture

```text
Win-Brightness/src/
├── app/          Application lifecycle and registry settings
├── brightness/   Monitor catalog, controller, DDC/CI, and software dimming
├── platform/     Small Win32 RAII and DPI helpers
├── resources/    Application icon and Win32 resource identifiers
└── ui/           Island host, native renderer/animation modules, and dimming overlays
```

The application uses Win32, Direct2D, DirectWrite, D3D11/DXGI, DirectComposition, DXVA2 monitor APIs, and the Windows registry. The UI thread owns rendering and dimming windows. The existing background worker owns physical monitor handles, enumeration, DDC/CI queries, writes, and retry scheduling; it publishes results through window messages. A separate UI pacing thread waits for requested DXGI/DWM frame boundaries and sleeps when the island is idle. Brightness input is throttled to one dispatch per 70 ms during a drag and flushed on release.

See [docs/UI.md](docs/UI.md) for module integration, dimensions, colors, motion parameters, and implementation limits. See [tests/README.md](tests/README.md) for the Windows regression suite. The preview above comes from the actual renderer with synthetic monitor data; it does not query real displays.
