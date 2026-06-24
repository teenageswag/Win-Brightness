# 💡 Win-Brightness — Windows Brightness Control

> A lightweight application for controlling monitor brightness directly from the system tray.

---

## ✨ Features

- 🖥️ **Hardware Mode** — manages brightness via DDC/CI (physical monitor control)
- 🌑 **Software Mode** — dims the screen using a transparent overlay
- 🔁 **Automatic Fallback** — if DDC/CI is unavailable, the app automatically switches to software mode
- 📌 **Tray Icon** — quick access with the current brightness level displayed in the tooltip
- 💾 **Settings Persistence** — brightness, mode, and state are saved in the registry between restarts
- 🚀 **Run at Startup** — optional launch on Windows boot
- 🎛️ **Popup Slider** — click the tray icon to open a convenient slider popup

---

## 🖱️ Usage

| Action                       | Result                        |
| ---------------------------- | ----------------------------- |
| **Left Click** on tray icon  | Show / hide brightness slider |
| **Right Click** on tray icon | Open context menu             |
| Drag the slider              | Adjust brightness (1–100%)    |
| Toggle in popup              | Enable / disable dimming      |

### Context Menu

- **Run at startup** — add to or remove from Windows startup
- **Brightness mode → Hardware / Software** — select the brightness control mode
- **Exit** — close the application

---

## 🏗️ Building

**Requirements:**

- Windows 10 / 11
- Visual Studio 2022/2026 (or a compatible MSVC toolchain)
- Windows SDK

**Steps:**

1. Open `Win-Brightness.slnx` in Visual Studio
2. Select the `Release | x64` configuration
3. Build the project (`Ctrl+Shift+B`)

The compiled binary will be located in `build/bin/x64/`.

---

## 📁 Project Structure

```
Win-Brightness/
├── Win-Brightness/src/
│   ├── main.cpp / main.h          # Entry point, GDI+ initialization
│   ├── app/
│   │   ├── App.cpp / App.h        # Main application class, message loop
│   │   └── SettingsStore          # Registry-based settings persistence
│   ├── brightness/
│   │   ├── BrightnessController   # Thread-safe brightness controller
│   │   ├── HardwareBrightness     # DDC/CI via physical monitors
│   │   ├── SoftwareBrightness     # Win32-based dimming overlay
│   │   └── BrightnessTypes.h      # Types and constants
│   ├── ui/
│   │   ├── PopupView              # Popup slider (GDI+, DPI-aware)
│   │   └── DimOverlay             # Transparent overlay window
│   ├── platform/
│   │   └── Win32Helpers           # Win32 utilities
│   └── resources/                 # Icon and assets
└── build/                         # Build artifacts

```

---

## ⚙️ Technologies

- **C++17** — core language
- **Win32 API** — windows, tray, messages
- **GDI+ (Gdiplus)** — UI rendering
- **DDC/CI** — hardware brightness control
- **Windows Registry** — settings storage
- **Multithreading** — background worker for applying brightness without blocking the UI
