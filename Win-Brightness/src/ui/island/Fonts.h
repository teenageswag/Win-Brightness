#pragma once

#include <windows.h>
#include <dwrite.h>
#include <memory>

namespace island {
class Fonts {
public:
    Fonts();
    ~Fonts();
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;

    HRESULT Initialize(IDWriteFactory* factory, HMODULE module);
    HRESULT CreateFormat(float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** format) const;
    bool UsesInter() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace island
