#include "main.h"

namespace {
    void EnableDpiAwarenessContext() {
        using SetProcessDpiAwarenessContextProc = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

        HMODULE user32 = GetModuleHandle(L"user32.dll");
        if (!user32) {
            return;
        }

        const auto setProcessDpiAwarenessContext = reinterpret_cast<SetProcessDpiAwarenessContextProc>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setProcessDpiAwarenessContext) {
            setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }
} // namespace

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    
    EnableDpiAwarenessContext();
    
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    const Gdiplus::Status gdiplusStatus = Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    if (gdiplusStatus != Gdiplus::Ok) {
        MessageBox(nullptr, L"Unable to initialize the Windows graphics subsystem.", L"Win-Brightness", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    int exitCode = 0;
    {
      App app(hInstance);
      if (app.Init()) {
        exitCode = app.Run();
      } else {
        exitCode = 1;
      }
    }
    
    Gdiplus::GdiplusShutdown(gdiplusToken);
    
    return exitCode;
}
