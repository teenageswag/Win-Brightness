# Windows regression tests

Build with a recent CMake, MSVC with C++23 support, and the Windows SDK:

```powershell
cmake -S tests -B build/tests -G "Visual Studio 18 2026" -A x64
cmake --build build/tests --config Release --parallel
ctest --test-dir build/tests -C Release --output-on-failure
```

The eight test executables cover:

- `SettingsTests`: malformed registry strings, unsigned brightness bounds, settings round trips, access-denied errors, startup commands, saving only changed values, and validated UI preference storage.
- `HardwareTests`: partial physical-monitor failures, captured error codes, duplicate write caching, handle cleanup under allocation failure, and failed APIs that supply no error code.
- `ControllerTests`: asynchronous enumeration, bounded recovery, unsupported displays, preserving a catalog after failure, hardware/software transitions, and cancellation between slow writes.
- `IslandMathTests`: refresh-rate-independent springs, retargeting without velocity discontinuities, superellipse boundaries, five-row layout, scrolling, and narrow-screen footer layout.
- `FontTests`: private embedded Inter collection, real weight matching, and missing-resource fallback.
- `RendererTests`: the production Direct2D/DirectComposition path, actual embedded fonts, light/dark modes, five-row scrolling, hardware status labels, narrow-screen layout, and DPI resizing.
- `PopupTests`: pending brightness across state synchronization, commit ordering, exact top anchoring, DPI sizing, capture loss, cancelled drags, throttle dispatches, unplug cancellation, stable monitor identity after reorder, input during morphs, interrupted animations, foreground preservation, temporary shortcut cleanup, idle scheduling/CPU measurements, and GDI/USER resource cleanup over repeated creation.
- `OverlayTests`: UI-thread ownership, opacity changes without moves or repaints, geometry changes, selection changes, and window cleanup.

Monitor I/O tests supply fake DXVA2 endpoints. Overlay and renderer tests create windows outside the visible desktop. Popup tests briefly show a synthetic island and use callbacks without monitor I/O. Settings tests redirect their own process's HKCU to a disposable subtree, restore its test ACL, and delete the subtree on exit. They do not use the application's settings or real monitor brightness. Running SettingsTests needs permission to create its temporary HKCU subtree.

To export real renderer frames for visual review, create an output directory and pass it to RendererTests:

```powershell
New-Item -ItemType Directory -Force build/island-preview
& build/tests/Release/RendererTests.exe "$PWD/build/island-preview"
```

PNG readback is opt-in and happens before swap-chain presentation; the normal application performs no GPU readback. Images are cropped to the current window and use synthetic monitor data. PopupTests prints three one-second process CPU samples after rendering settles; this is an observation, not a timing-sensitive pass/fail assertion.

Physical hot-plug, sleep/resume, monitor firmware behavior, and mixed-DPI rendering still need validation on Windows 10/11 with actual displays.
