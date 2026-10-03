# Native dynamic island

The replacement UI uses WinAPI + Direct2D/DirectWrite + DirectComposition. It keeps `PopupState` and `PopupActions` as the boundary to the existing brightness controller. Each monitor has its own target. No physical-monitor API is called by the renderer, layout, animation, dismissal, or pacing modules.

## Modules and integration

| Module | Responsibility |
| --- | --- |
| `ui/PopupModel.h` | Per-monitor controller snapshot and callbacks with stable display IDs |
| `ui/PopupView.*` | HWND, input/capture, stable monitor IDs, opening/visible/closing states, throttle, DPI/top anchoring, graphics recovery |
| `ui/island/Layout.h` | DIP layout, five-row viewport, scroll bounds, hit rectangles, narrow-screen footer |
| `ui/island/Geometry.h` | Superellipse lower corners, square top corners, and matching contour hit test |
| `ui/island/Spring.h` | Analytical spring position/velocity integration and interruptible retargeting |
| `ui/island/Fonts.*` | Private DirectWrite collection backed by immutable embedded resources |
| `ui/island/Renderer.*` | D3D/DXGI buffers, D2D drawing/cached geometry/text/shadow, DComp alpha composition |
| `ui/island/FrameClock.*` | Requested DXGI/DWM pacing; no controller or renderer state shared with its thread |
| `ui/island/Dismissal.h` | Monotonic 3.5-second inactivity deadline |
| `ui/island/Preferences.*` | Separate validated UI preference registry values |

`App::SyncPopup` supplies monitor snapshots with individual brightness values. Callbacks apply a target by stable display ID, enabled state, and mode; other callbacks report UI failures, show the tray menu, and identify the tray icon hit area. There is no group-selection callback. `App::Run` waits on messages and the optional frame-ready event through `MsgWaitForMultipleObjectsEx`; `FrameWaitHandle()` returns no handle while hidden or settled. Do not replace this with a permanent timer.

The `.vcxproj` includes all modules and font resources. Link dependencies are declared in the native source files. The application no longer starts GDI+. COM initialization belongs to the renderer on the UI thread. Copy `Inter-LICENSE.txt` alongside the executable when distributing; the project does this after building.

The HWND uses `WS_POPUP`, `WS_EX_TOPMOST`, `WS_EX_TOOLWINDOW`, `WS_EX_NOACTIVATE`, and `WS_EX_NOREDIRECTIONBITMAP`. The transparent area comes from a premultiplied BGRA composition surface, not `SetWindowRgn`. The pacing thread is stopped and joined before the renderer releases its frame-latency handle or swap chain. Raw mouse input is registered only while the panel is open; it observes button presses without `RIDEV_NOLEGACY`, hooks, focus changes, or input suppression. Registration is removed when closing.

## Dimensions and typography

All values below are DIP. The shell's top is exactly `rcMonitor.top`; the 16-DIP shadow allocation exists only at the sides and bottom. Placement is centered on the primary monitor, including negative desktop coordinates.

| Metric | Value |
| --- | --- |
| Panel width | 460, reduced to fit available screen width |
| Panel height | 64 + 77 × visible rows; two rows = 218, five = 449 |
| Visible rows | At most 5; smaller available height further limits the viewport |
| Outer padding | 15 on all sides |
| Monitor label height | 20 |
| Label-to-slider gap | 10 |
| Slider height | 32; value inset 10 horizontally / 8 vertically |
| Between monitor rows | 15 |
| Footer buttons | 34 high; 13-DIP text, 7-DIP vertical padding; width budget includes 15-DIP horizontal padding |
| Narrow footer | Below 350 DIP available width: mode selector and actions on separate rows, 15 between rows |

Monitor names use Inter Medium 14, hardware status uses Inter Regular 12 with +0.1 DIP character spacing, and buttons/percentages use Inter Medium 13. Text stays on floating-point DIP coordinates. Tabular figure typography is enabled. DirectWrite grayscale antialiasing is used for alpha surfaces: ClearType subpixel color fringes would not compose correctly over an unknown background.

Fonts are embedded Inter 4.1 with a Segoe UI fallback. SF Pro is not included because [Apple's published font license](https://developer.apple.com/fonts/) does not authorize bundling it for this Windows interface. Font provenance and the redistribution license live in `src/resources/fonts/`.

Native-renderer previews with synthetic independent targets: [dark](../image/island-dark.png) and [light](../image/island-light.png).

## Colors and material

| Role | Dark | Light |
| --- | --- | --- |
| Background | `#080809` | `#F5F5F7` |
| Button surface | `#202022` | `#E5E5EA` |
| Main text | `#F5F5F7` | `#171719` |
| Secondary text | `#A1A1AA` | `#62626B` |
| Slider track | `#303034` | `#55555E` |
| Slider fill | `#FFFFFF` | `#FFFFFF` |
| Text over fill | `#171719` | `#171719` |
| Selected mode | `#FFFFFF` | `#1C1C1E` |
| Selected mode text | `#171719` | `#FFFFFF` |
| Error | `#FF6961` | `#B42318` |
| Inner border | `#FFFFFF`, 10% alpha | `#000000`, 8% alpha |
| Shadow | Black, 24% alpha | Black, 14% alpha |

The inner outline is 1 DIP. The shadow uses a cached Direct2D shadow effect. System theme changes refresh colors; Windows High Contrast substitutes system colors. Translucency is optional and defaults off; enabled background alpha is 94% dark / 96% light and respects Windows' transparency preference. This is alpha blending, without Acrylic/Mica desktop blur.

## Motion and scheduling

The corner exponent is `n = 4.5`. The shell has a straight top edge and square upper corners. Two local superellipse quadrants form the lower corners, using 64 vector segments per quadrant and Direct2D antialiasing. The shell radius is 14% of its height, capped at 32 DIP. Controls use four rounded quadrants and a radius limited by their dimensions.

| Spring | Stiffness | Damping | Mass |
| --- | --- | --- | --- |
| Size on catalog/geometry changes | 440 | 42 | 1 |
| Entrance/exit progress, mode, hover/press, number feedback | 980 | 63 | 1 |
| External/keyboard brightness rail | 1540 | 79 | 1 |

Targets preserve current position and velocity. Integration uses QPC delta time and an exact analytical solution, avoiding fixed-step dependence on refresh rate. The panel opens directly with all rows. A spring progress value drives a DirectComposition translation from `-(height + 16)` DIP to zero, top-center scale from 0.97 to 1, and opacity from zero to one during the first third of the travel. Exit follows the same path in reverse; reopening during exit continues from the current presentation. Input coordinates are transformed back through the same scale/translation, so hit testing follows what is visible. The composition visual is concealed before a hidden HWND is shown again, preventing an old-frame flash.

Each row keeps its own slider and number-feedback springs by stable ID. Pointer drags track fractional position immediately; outgoing brightness is quantized to the controller's integer range. Number changes use a small 1.5-DIP spring feedback while the target percentage updates immediately. System reduced-motion settings and the tray animation switch snap transitions to their targets.

The pacing thread waits for a request, the DXGI latency event, and `DwmFlush`, then signals the UI. There is no periodic animation timer. A 3.5-second dismissal timer uses a monotonic inactivity deadline: keyboard, pointer, wheel, and button input reset it; capture and the panel context menu suspend it. Monitor status synchronization does not reset the deadline. An outside click closes through the same spring animation and remains available to the foreground application. A click on the tray icon is handled by the normal toggle, avoiding a close/reopen race.

A one-shot 70-ms throttle timer exists only while brightness input is pending; release flushes the last target. Pending values are stored per display, so a catalog/status update cannot overwrite another row's optimistic value. The regression test's 100 rapid pointer updates dispatch one latest value on the timer and one final value on release. DDC/CI remains on the controller's worker thread.

The renderer keeps a maximum-sized surface during size changes and resizes buffers only when DPI or available height changes. Geometry, text layouts, and the shadow are cached. Size changes affect most of the small surface, so rendering redraws that surface; steady states need no redraw at all rather than applying a permanent dirty-rectangle loop.

## Validation and remaining limits

The eight regression executables use fake monitor endpoints and synthetic UI state. Native rendering is exercised in light/dark themes, at 96/144 DPI, with five-row overflow and a narrow footer. Input tests cover capture cancellation, disappearing/reordered monitor IDs, independent pending values, unavailable hardware rows, entrance/exit reversal, foreground preservation, throttling, idle suspension, outside-click dismissal, raw input cleanup, and the live 3.5-second timer. Six repeated popup creation/destruction cycles show no growth in GDI/USER object counts. A local run measured 0.000 ms process CPU in each of three one-second idle samples; driver initialization can contribute CPU immediately after startup, so this is not a universal hardware guarantee.

The controller now stores targets by stable monitor ID and applies them independently. An unavailable hardware monitor is skipped and its target remains unchanged. Software overlay alpha is computed separately for each monitor; a 100-percent display removes only its own overlay. Targets survive catalog refresh/reorder, disconnection/reconnection, and application restart through the settings store. Both modes use the same saved per-monitor target; the former shared brightness remains only the default for a display without its own saved value. Percentages are requested targets, not continuously polled hardware readbacks.

Native tooltip text and an accessible window name expose names and focused actions, but a complete custom UI Automation provider is not implemented. Physical hot-plug, sleep/resume, firmware-specific DDC/CI behavior, and mixed-DPI rendering still require testing on real Windows 10/11 displays.
