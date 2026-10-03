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
    
    int exitCode = 0;
    {
      App app(hInstance);
      if (app.Init()) {
        exitCode = app.Run();
      } else {
        exitCode = 1;
      }
    }
    
    return exitCode;
}
