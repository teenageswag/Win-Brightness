#include "main.h"

void EnableDpiAwarenessContext()
{
    typedef BOOL(WINAPI * SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser32 = GetModuleHandle(L"user32.dll");
    if (hUser32)
    {
        SetProcessDpiAwarenessContextProc pSetProcessDpiAwarenessContext =
            (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetProcessDpiAwarenessContext)
        {
            pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    EnableDpiAwarenessContext();

    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    int exitCode = 0;
    {
        App app(hInstance);
        if (app.Init())
        {
            exitCode = app.Run();
        }
        else
        {
            exitCode = 1;
        }
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);

    return exitCode;
}
