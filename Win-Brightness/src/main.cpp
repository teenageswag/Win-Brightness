#include "main.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    
    EnableDpiAwarenessContext();
    
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    const Gdiplus::Status gdiplusStatus = Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    
    int exitCode = 0;
    {
      App app(hInstance);
      if (app.Init()) {
        exitCode = app.Run();
      } else {
        exitCode = 1;
      }
    }
    
    if (gdiplusStatus == Gdiplus::Ok) {
      Gdiplus::GdiplusShutdown(gdiplusToken);
    }
    
    return exitCode;
}
