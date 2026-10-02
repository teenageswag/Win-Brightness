#include "ui/island/Fonts.h"
#include <wrl.h>
#include <cstdio>
#include <stdexcept>

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() try {
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(factory.GetAddressOf()))), "create DirectWrite");
    island::Fonts fonts;
    Check(SUCCEEDED(fonts.Initialize(factory.Get(), GetModuleHandleW(nullptr))) && fonts.UsesInter(), "load embedded Inter");
    for (auto weight : {DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_WEIGHT_SEMI_BOLD}) {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        Check(SUCCEEDED(fonts.CreateFormat(14.0f, weight, &format)), "create embedded font format");
        Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
        Check(SUCCEEDED(format->GetFontCollection(&collection)) && collection, "custom font collection");
        UINT32 index = 0; BOOL exists = FALSE;
        Check(SUCCEEDED(collection->FindFamilyName(L"Inter", &index, &exists)) && exists, "Inter family present");
        Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
        Microsoft::WRL::ComPtr<IDWriteFont> face;
        Check(SUCCEEDED(collection->GetFontFamily(index, &family)) &&
              SUCCEEDED(family->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL,
                  DWRITE_FONT_STYLE_NORMAL, &face)) && face->GetWeight() == weight &&
              face->GetSimulations() == DWRITE_FONT_SIMULATIONS_NONE, "real font weight without synthesized bold");
    }
    island::Fonts fallback;
    Check(fallback.Initialize(factory.Get(), GetModuleHandleW(L"kernel32.dll")) == S_FALSE && !fallback.UsesInter(),
          "missing font falls back to Segoe UI");
    std::puts("FontTests passed");
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
