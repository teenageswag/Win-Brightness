# Windows regression tests

Build with a recent CMake, MSVC with C++23 support, and the Windows SDK:

```powershell
cmake -S tests -B build/tests -G "Visual Studio 18 2026" -A x64
cmake --build build/tests --config Release --parallel
ctest --test-dir build/tests -C Release --output-on-failure
```

The five test executables cover:

- `SettingsTests`: malformed registry strings, unsigned brightness bounds, settings round trips, access-denied errors, startup commands, and saving only changed values.
- `HardwareTests`: partial physical-monitor failures, captured error codes, duplicate write caching, handle cleanup under allocation failure, and failed APIs that supply no error code.
- `ControllerTests`: asynchronous enumeration, bounded recovery, unsupported displays, preserving a catalog after failure, hardware/software transitions, and cancellation between slow writes.
- `PopupTests`: pending brightness across state synchronization, commit ordering, DPI rectangles, capture loss, and cancelled drags.
- `OverlayTests`: UI-thread ownership, opacity changes without moves or repaints, geometry changes, selection changes, and window cleanup.

Monitor I/O tests supply fake DXVA2 endpoints. Overlay tests create tiny windows outside the visible desktop. Settings tests redirect their own process's HKCU to a disposable subtree, restore its test ACL, and delete the subtree on exit. They do not use the application's settings or real monitor brightness.

Physical hot-plug, sleep/resume, monitor firmware behavior, and mixed-DPI rendering still need validation on Windows 10/11 with actual displays.
