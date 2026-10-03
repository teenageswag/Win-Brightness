#include "Renderer.h"
#include "Fonts.h"
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <dwrite_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_3.h>
#include <wincodec.h>
#include <wrl.h>
#include <array>
#include <cwchar>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "advapi32.lib")

namespace island {
using Microsoft::WRL::ComPtr;
namespace {
D2D1_COLOR_F Color(UINT32 hex, float alpha = 1.0f) { return D2D1::ColorF(hex, alpha); }
D2D1_RECT_F Native(Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); }
D2D1_COLOR_F Mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
D2D1_COLOR_F SystemColor(int index) {
    const COLORREF value = GetSysColor(index);
    return {GetRValue(value) / 255.0f, GetGValue(value) / 255.0f, GetBValue(value) / 255.0f, 1.0f};
}
struct Palette {
    D2D1_COLOR_F background, surface, text, muted, track, fill, fillText, selected, selectedText, border, error;
    float shadow;
};
Palette MakePalette(bool light, bool contrast, bool translucent) {
    if (contrast) {
        return {SystemColor(COLOR_WINDOW), SystemColor(COLOR_BTNFACE), SystemColor(COLOR_WINDOWTEXT),
            SystemColor(COLOR_WINDOWTEXT), SystemColor(COLOR_BTNFACE), SystemColor(COLOR_HIGHLIGHT),
            SystemColor(COLOR_HIGHLIGHTTEXT), SystemColor(COLOR_HIGHLIGHT), SystemColor(COLOR_HIGHLIGHTTEXT),
            SystemColor(COLOR_WINDOWTEXT), SystemColor(COLOR_WINDOWTEXT), 0.0f};
    }
    if (light) return {Color(0xF5F5F7, translucent ? 0.96f : 1.0f), Color(0xE5E5EA),
        Color(0x171719), Color(0x62626B), Color(0x55555E), Color(0xFFFFFF), Color(0x171719),
        Color(0x1C1C1E), Color(0xFFFFFF), Color(0, 0.08f), Color(0xB42318), 0.14f};
    return {Color(0x080809, translucent ? 0.94f : 1.0f), Color(0x202022),
        Color(0xF5F5F7), Color(0xA1A1AA), Color(0x303034), Color(0xFFFFFF), Color(0x171719),
        Color(0xFFFFFF), Color(0x171719), Color(0xFFFFFF, 0.10f), Color(0xFF6961), 0.24f};
}
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
struct Shape {
    float width = -1.0f, height = -1.0f, radius = -1.0f;
    bool squareTop = false;
    ComPtr<ID2D1PathGeometry> path;
};
struct Text {
    std::wstring value;
    float width = 0.0f, height = 0.0f, size = 0.0f;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    bool centered = false;
    ComPtr<IDWriteTextLayout> layout;
};
} // namespace

struct Renderer::Impl {
    Apartment apartment;
    HWND window = nullptr;
    HINSTANCE module = nullptr;
    UINT dpi = 96, width = 0, height = 0;
    float canvasWidth = 0.0f;
    bool systemLight = false, highContrast = false, systemTransparency = true;
    HANDLE latency = nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<ID2D1DeviceContext> context;
    ComPtr<IDXGISwapChain2> swapchain;
    ComPtr<ID2D1Bitmap1> target;
    ComPtr<ID2D1Bitmap1> capture;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<IDCompositionTarget> compositionTarget;
    ComPtr<IDCompositionVisual> visual;
    ComPtr<IDCompositionEffectGroup> visualEffect;
    ComPtr<IDWriteFactory> write;
    Fonts fonts;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1Effect> shadow;
    ComPtr<ID2D1CommandList> shadowSource;
    float shadowWidth = -1.0f, shadowHeight = -1.0f, shadowRadius = -1.0f;
    std::vector<Shape> shapes;
    std::vector<Text> texts;
    size_t shapeCursor = 0, textCursor = 0;
    HRESULT drawingError = S_OK;

    ~Impl() {
        if (context) context->SetTarget(nullptr);
        if (latency) CloseHandle(latency);
    }

    void Fail(HRESULT error) { if (FAILED(error) && SUCCEEDED(drawingError)) drawingError = error; }
    ID2D1PathGeometry* Geometry(float w, float h, float r, bool squareTop = false) {
        if (shapeCursor == shapes.size()) shapes.emplace_back();
        auto& shape = shapes[shapeCursor++];
        if (shape.width == w && shape.height == h && shape.radius == r && shape.squareTop == squareTop && shape.path) return shape.path.Get();
        shape.path.Reset();
        Fail(factory->CreatePathGeometry(&shape.path));
        if (!shape.path) return nullptr;
        ComPtr<ID2D1GeometrySink> sink;
        Fail(shape.path->Open(&sink));
        if (!sink) return nullptr;
        const auto points = Squircle(w, h, r, squareTop);
        sink->BeginFigure(D2D1::Point2F(points[0].x, points[0].y), D2D1_FIGURE_BEGIN_FILLED);
        for (size_t i = 1; i < points.size(); ++i) sink->AddLine(D2D1::Point2F(points[i].x, points[i].y));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        Fail(sink->Close());
        shape.width = w; shape.height = h; shape.radius = r; shape.squareTop = squareTop;
        return shape.path.Get();
    }

    void ShapeFill(Rect rect, float radius, D2D1_COLOR_F color, float scale = 1.0f, bool stroke = false, bool squareTop = false) {
        if (rect.Width() <= 0.0f || rect.Height() <= 0.0f) return;
        auto* geometry = Geometry(rect.Width(), rect.Height(), radius, squareTop);
        if (!geometry) return;
        D2D1_MATRIX_3X2_F previous{};
        context->GetTransform(&previous);
        const auto transform = D2D1::Matrix3x2F::Scale(scale, scale,
            D2D1::Point2F(rect.Width() * 0.5f, rect.Height() * 0.5f)) *
            D2D1::Matrix3x2F::Translation(rect.left, rect.top) * previous;
        context->SetTransform(transform);
        brush->SetColor(color);
        if (stroke) context->DrawGeometry(geometry, brush.Get(), 1.0f);
        else context->FillGeometry(geometry, brush.Get());
        context->SetTransform(previous);
    }

    void Label(const std::wstring& value, Rect rect, float size, DWRITE_FONT_WEIGHT weight,
               D2D1_COLOR_F color, bool centered = false) {
        if (rect.Width() <= 0.0f || rect.Height() <= 0.0f) return;
        if (textCursor == texts.size()) texts.emplace_back();
        auto& text = texts[textCursor++];
        if (!text.layout || text.value != value || text.width != rect.Width() || text.height != rect.Height() ||
            text.size != size || text.weight != weight || text.centered != centered) {
            text.layout.Reset();
            ComPtr<IDWriteTextFormat> format;
            Fail(fonts.CreateFormat(size, weight, &format));
            if (!format) return;
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            format->SetTextAlignment(centered ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
            Fail(write->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), format.Get(),
                                        rect.Width(), rect.Height(), &text.layout));
            if (!text.layout) return;
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            ComPtr<IDWriteInlineObject> ellipsis;
            Fail(write->CreateEllipsisTrimmingSign(format.Get(), &ellipsis));
            Fail(text.layout->SetTrimming(&trimming, ellipsis.Get()));
            ComPtr<IDWriteTypography> typography;
            Fail(write->CreateTypography(&typography));
            if (typography) {
                Fail(typography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1}));
                Fail(text.layout->SetTypography(typography.Get(), {0, static_cast<UINT32>(value.size())}));
            }
            if (size == 12.0f) {
                ComPtr<IDWriteTextLayout1> spacing;
                if (SUCCEEDED(text.layout.As(&spacing)))
                    Fail(spacing->SetCharacterSpacing(0.1f, 0.0f, 0.0f, {0, static_cast<UINT32>(value.size())}));
            }
            text.value = value; text.width = rect.Width(); text.height = rect.Height();
            text.size = size; text.weight = weight; text.centered = centered;
        }
        brush->SetColor(color);
        context->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), text.layout.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    float Scale(Target control, const Frame& frame) const { return control == frame.hot ? frame.hotScale : 1.0f; }
    void Focus(Rect rect, Target control, const Frame& frame, const Palette& palette) {
        if (frame.keyboardFocus && control == frame.focus) {
            rect = {rect.left - 2, rect.top - 2, rect.right + 2, rect.bottom + 2};
            ShapeFill(rect, std::min(12.0f, rect.Height() * 0.5f), palette.text, 1.0f, true);
        }
    }
    void Slider(const Row& row, const Frame& frame, const Palette& palette) {
        const PopupState& state = *frame.state;
        const bool selected = row.index < state.monitors.size() && (state.mode == BrightnessMode::Software ||
            (state.monitors[row.index].hardwareBrightness && state.monitors[row.index].hardwareStatus == HardwareStatus::Available));
        const int percentValue = row.index < state.monitors.size() ? state.monitors[row.index].brightness : kDefaultBrightness;
        const float railValue = row.index < frame.rowValues.size() ? frame.rowValues[row.index] : static_cast<float>(percentValue);
        const float numberFeedback = row.index < frame.rowFeedback.size() ? frame.rowFeedback[row.index] : 0;
        const float alpha = state.enabled && selected ? 1.0f : 0.45f;
        auto track = palette.track; track.a *= alpha;
        ShapeFill(row.slider, row.slider.Height() * 0.5f, track, Scale({Control::Slider, row.index}, frame));
        Rect fill = row.slider;
        fill.right = fill.left + std::clamp(railValue / 100.0f, 0.0f, 1.0f) * fill.Width();
        auto accent = palette.fill; accent.a *= alpha;
        if (selected) ShapeFill(fill, std::min(fill.Height() * 0.5f, fill.Width() * 0.5f), accent);
        {
            wchar_t percent[16]{};
            if (selected) swprintf_s(percent, L"%d%%", percentValue);
            else swprintf_s(percent, L"\x2014");
            Rect number{row.slider.right - 66.0f, row.slider.top + 8.0f - numberFeedback * 1.5f,
                        row.slider.right - 10.0f, row.slider.bottom - 8.0f - numberFeedback * 1.5f};
            Label(percent, number, 13, DWRITE_FONT_WEIGHT_MEDIUM,
                  highContrast ? palette.text : Color(0xFFFFFF), true);
            if (selected) {
                context->PushAxisAlignedClip(Native(fill), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                Label(percent, number, 13, DWRITE_FONT_WEIGHT_MEDIUM, palette.fillText, true);
                context->PopAxisAlignedClip();
            }
        }
        Focus(row.slider, {Control::Slider, row.index}, frame, palette);
    }

    void Content(const Layout& layout, const Frame& frame, const Palette& palette, float alpha) {
        if (alpha < 0.001f) return;
        D2D1_LAYER_PARAMETERS1 layer = D2D1::LayerParameters1();
        layer.opacity = alpha;
        context->PushLayer(layer, nullptr);
        const auto& state = *frame.state;
        {
            context->PushAxisAlignedClip(Native(layout.viewport), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            for (const auto& row : layout.rows) {
                if (row.slider.bottom < layout.viewport.top || row.label.top > layout.viewport.bottom) continue;
                const auto& monitor = state.monitors[row.index];
                Rect label = row.label;
                label.right -= state.mode == BrightnessMode::Hardware ? 82.0f : 0.0f;
                Label(monitor.name, label, 14, DWRITE_FONT_WEIGHT_MEDIUM, palette.text);
                if (state.mode == BrightnessMode::Hardware) {
                    const bool failed = monitor.hardwareStatus == HardwareStatus::Failed;
                    const bool unsupported = monitor.hardwareStatus == HardwareStatus::Unsupported;
                    const wchar_t* status = failed ? L"Failed" : unsupported ? L"No DDC" :
                        monitor.hardwareStatus == HardwareStatus::Available ? L"DDC/CI" : L"Checking";
                    Label(status, {row.label.right - 78, row.label.top, row.label.right, row.label.bottom},
                          12, DWRITE_FONT_WEIGHT_NORMAL, failed || unsupported ? palette.error : palette.muted, true);
                }
                Slider(row, frame, palette);
                Focus(row.label, {Control::Monitor, row.index}, frame, palette);
            }
            if (state.monitors.empty()) {
                Label(state.catalogError ? L"Unable to enumerate displays" : L"Detecting displays\x2026",
                      layout.viewport, 14, DWRITE_FONT_WEIGHT_MEDIUM, state.catalogError ? palette.error : palette.muted, true);
            }
            if (layout.MaximumScroll() > 0.0f) {
                const float contentHeight = static_cast<float>(layout.rows.size()) * kRowStride - kRowGap;
                const float barHeight = std::max(20.0f, layout.viewport.Height() * layout.viewport.Height() / contentHeight);
                const float scroll = layout.viewport.top - layout.rows.front().label.top;
                const float barTop = layout.viewport.top + scroll / layout.MaximumScroll() * (layout.viewport.Height() - barHeight);
                ShapeFill({layout.viewport.right - 2, barTop, layout.viewport.right, barTop + barHeight}, 1, palette.muted);
            }
            context->PopAxisAlignedClip();
            Rect modes{layout.software.left, layout.software.top, layout.hardware.right, layout.hardware.bottom};
            ShapeFill(modes, modes.Height() * 0.5f, palette.surface);
            Rect active = layout.software.Offset(frame.modePosition * layout.software.Width(), 0);
            const Target selectedMode{state.mode == BrightnessMode::Software ? Control::Software : Control::Hardware};
            ShapeFill(active, active.Height() * 0.5f, palette.selected, Scale(selectedMode, frame));
            if (frame.hot.control == Control::Software && state.mode == BrightnessMode::Hardware)
                ShapeFill(layout.software, 17, Mix(palette.surface, palette.text, 0.08f), frame.hotScale);
            if (frame.hot.control == Control::Hardware && state.mode == BrightnessMode::Software)
                ShapeFill(layout.hardware, 17, Mix(palette.surface, palette.text, 0.08f), frame.hotScale);
            Label(L"Software", layout.software, 13, DWRITE_FONT_WEIGHT_MEDIUM,
                  Mix(palette.selectedText, palette.text, frame.modePosition), true);
            Label(L"Hardware", layout.hardware, 13, DWRITE_FONT_WEIGHT_MEDIUM,
                  Mix(palette.text, palette.selectedText, frame.modePosition), true);
            ShapeFill(layout.power, 17, palette.surface, Scale({Control::Power}, frame));
            Label(state.enabled ? L"Disable" : L"Enable", layout.power, 13, DWRITE_FONT_WEIGHT_MEDIUM, palette.text, true);
            Focus(layout.software, {Control::Software}, frame, palette);
            Focus(layout.hardware, {Control::Hardware}, frame, palette);
        }
        Focus(layout.power, {Control::Power}, frame, palette);
        context->PopLayer();
    }

    HRESULT TargetBitmap() {
        ComPtr<IDXGISurface> surface;
        HRESULT result = swapchain->GetBuffer(0, IID_PPV_ARGS(&surface));
        if (FAILED(result)) return result;
        const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<float>(dpi), static_cast<float>(dpi));
        result = context->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &target);
        if (SUCCEEDED(result)) context->SetTarget(target.Get());
        return result;
    }
};

Renderer::Renderer() : m_impl(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;
HANDLE Renderer::FrameHandle() const { return m_impl->latency; }
bool Renderer::UsesInter() const { return m_impl->fonts.UsesInter(); }

void Renderer::RefreshTheme() {
    DWORD light = 0, size = sizeof(light);
    m_impl->systemLight = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
        RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS && light != 0;
    DWORD transparency = 1; size = sizeof(transparency);
    m_impl->systemTransparency = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency",
        RRF_RT_REG_DWORD, nullptr, &transparency, &size) != ERROR_SUCCESS || transparency != 0;
    HIGHCONTRASTW contrast{sizeof(contrast)};
    m_impl->highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

HRESULT Renderer::Initialize(HWND window, HINSTANCE module, UINT dpi, float maximumHeight) {
    auto& r = *m_impl;
    if (FAILED(r.apartment.result) && r.apartment.result != RPC_E_CHANGED_MODE) return r.apartment.result;
    r.window = window; r.module = module; r.dpi = dpi;
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
        D3D11_SDK_VERSION, &r.device, nullptr, &r.immediate);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
        D3D11_SDK_VERSION, &r.device, nullptr, &r.immediate);
    if (FAILED(hr)) return hr;
    D2D1_FACTORY_OPTIONS options{};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
                          reinterpret_cast<void**>(r.factory.GetAddressOf()));
    if (FAILED(hr)) return hr;
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(hr = r.device.As(&dxgiDevice))) return hr;
    if (FAILED(hr = r.factory->CreateDevice(dxgiDevice.Get(), &r.d2dDevice))) return hr;
    if (FAILED(hr = r.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &r.context))) return hr;
    r.context->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    r.context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (FAILED(hr = r.context->CreateSolidColorBrush(Color(0), &r.brush))) return hr;
    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
                                      reinterpret_cast<IUnknown**>(r.write.GetAddressOf())))) return hr;
    // Keep rendering with Segoe UI if the embedded collection cannot be read.
    r.fonts.Initialize(r.write.Get(), module);
    if (FAILED(hr = r.context->CreateEffect(CLSID_D2D1Shadow, &r.shadow))) return hr;
    if (FAILED(hr = r.shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, 4.0f))) return hr;
    if (FAILED(hr = DCompositionCreateDevice(dxgiDevice.Get(), __uuidof(IDCompositionDevice),
                                            reinterpret_cast<void**>(r.composition.GetAddressOf())))) return hr;
    if (FAILED(hr = r.composition->CreateTargetForHwnd(window, TRUE, &r.compositionTarget))) return hr;
    if (FAILED(hr = r.composition->CreateVisual(&r.visual))) return hr;
    if (FAILED(hr = r.composition->CreateEffectGroup(&r.visualEffect))) return hr;
    if (FAILED(hr = r.visual->SetEffect(r.visualEffect.Get()))) return hr;
    if (FAILED(hr = r.compositionTarget->SetRoot(r.visual.Get()))) return hr;
    RefreshTheme();
    return Resize(dpi, maximumHeight);
}

HRESULT Renderer::Resize(UINT dpi, float maximumHeight) {
    auto& r = *m_impl;
    const float scale = static_cast<float>(dpi) / 96.0f;
    const UINT width = static_cast<UINT>(std::ceil((kWidth + kShadowMargin * 2) * scale));
    const UINT height = static_cast<UINT>(std::ceil((std::max(maximumHeight, kMinimumHeight) + kShadowMargin) * scale));
    r.dpi = dpi;
    r.canvasWidth = static_cast<float>(width) / scale;
    r.context->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    if (r.swapchain && r.width == width && r.height == height) return S_OK;
    r.context->SetTarget(nullptr);
    r.target.Reset(); r.capture.Reset();
    HRESULT hr = S_OK;
    if (r.swapchain) {
        hr = r.swapchain->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
                                       DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    } else {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(hr = r.device.As(&dxgiDevice))) return hr;
        if (FAILED(hr = dxgiDevice->GetAdapter(&adapter))) return hr;
        if (FAILED(hr = adapter->GetParent(IID_PPV_ARGS(&factory)))) return hr;
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = width; description.Height = height;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        description.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        ComPtr<IDXGISwapChain1> chain;
        if (FAILED(hr = factory->CreateSwapChainForComposition(r.device.Get(), &description, nullptr, &chain))) return hr;
        if (FAILED(hr = chain.As(&r.swapchain))) return hr;
        if (FAILED(hr = r.swapchain->SetMaximumFrameLatency(1))) return hr;
        r.latency = r.swapchain->GetFrameLatencyWaitableObject();
        if (!r.latency) return E_FAIL;
        if (FAILED(hr = r.visual->SetContent(r.swapchain.Get()))) return hr;
    }
    if (FAILED(hr)) return hr;
    r.width = width; r.height = height;
    if (FAILED(hr = r.TargetBitmap())) return hr;
    return r.composition->Commit();
}

HRESULT Renderer::Draw(const Frame& frame, const wchar_t* snapshotPath) {
    auto& r = *m_impl;
    if (!frame.state || !r.target) return E_UNEXPECTED;
    r.shapeCursor = 0; r.textCursor = 0; r.drawingError = S_OK;
    const Rect body = frame.layout.shell;
    const auto palette = MakePalette(frame.preferences.theme == Theme::Light ||
        (frame.preferences.theme == Theme::System && r.systemLight), r.highContrast,
        frame.preferences.translucent && r.systemTransparency);
    const float left = (r.canvasWidth - body.Width()) * 0.5f;
    auto* bodyGeometry = r.Geometry(body.Width(), body.Height(), frame.radius, true);
    if (!bodyGeometry) return r.drawingError;
    HRESULT hr = S_OK;
    if (!r.shadowSource || r.shadowWidth != body.Width() || r.shadowHeight != body.Height() || r.shadowRadius != frame.radius) {
        r.shadow->SetInput(0, nullptr);
        r.shadowSource.Reset();
        if (FAILED(hr = r.context->CreateCommandList(&r.shadowSource))) return hr;
        r.context->SetTarget(r.shadowSource.Get());
        r.context->SetTransform(D2D1::Matrix3x2F::Identity());
        r.context->BeginDraw();
        r.brush->SetColor(Color(0xFFFFFF));
        r.context->FillGeometry(bodyGeometry, r.brush.Get());
        hr = r.context->EndDraw();
        r.context->SetTarget(r.target.Get());
        if (FAILED(hr)) return hr;
        if (FAILED(hr = r.shadowSource->Close())) return hr;
        r.shadow->SetInput(0, r.shadowSource.Get());
        r.shadowWidth = body.Width(); r.shadowHeight = body.Height(); r.shadowRadius = frame.radius;
    }
    if (FAILED(hr = r.shadow->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(0, 0, 0, palette.shadow)))) return hr;
    r.context->SetTarget(r.target.Get());
    r.context->SetTransform(D2D1::Matrix3x2F::Identity());
    r.context->BeginDraw();
    r.context->Clear(Color(0, 0));
    r.context->DrawImage(r.shadow.Get(), D2D1::Point2F(left, 4.0f));
    r.context->SetTransform(D2D1::Matrix3x2F::Translation(left, 0));
    r.brush->SetColor(palette.background);
    r.context->FillGeometry(bodyGeometry, r.brush.Get());
    const auto layer = D2D1::LayerParameters1(Native(body), bodyGeometry,
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::Matrix3x2F::Identity(), 1.0f);
    r.context->PushLayer(layer, nullptr);
    r.Content(frame.layout, frame, palette, 1.0f);
    r.context->PopLayer();
    r.ShapeFill({0.5f, 0.5f, body.right - 0.5f, body.bottom - 0.5f},
                std::max(0.0f, frame.radius - 0.5f), palette.border, 1.0f, true, true);
    r.context->SetTransform(D2D1::Matrix3x2F::Identity());
    hr = r.context->EndDraw();
    if (FAILED(hr)) return hr;
    if (FAILED(r.drawingError)) return r.drawingError;
    // Readback is opt-in for visual tests and happens before Present rotates
    // buffers. Normal interactive rendering never maps a GPU texture or writes files.
    if (snapshotPath && FAILED(hr = SaveFramePng(snapshotPath))) return hr;
    RECT client{};
    if (!GetClientRect(r.window, &client)) return HRESULT_FROM_WIN32(GetLastError());
    if (FAILED(hr = r.visual->SetOffsetX(-static_cast<float>(static_cast<int>(r.width) - client.right) * 0.5f))) return hr;
    if (FAILED(hr = r.visualEffect->SetOpacity(frame.opacity))) return hr;
    if (FAILED(hr = r.composition->Commit())) return hr;
    DXGI_PRESENT_PARAMETERS parameters{};
    // Morphing changes the shell and its shadow, so those frames need the whole
    // small surface. Text layouts, control paths and the shadow are cached at rest.
    return r.swapchain->Present1(1, 0, &parameters);
}

HRESULT Renderer::SaveFramePng(const wchar_t* path) const {
    auto& r = *m_impl;
    if (!r.swapchain) return E_UNEXPECTED;
    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = r.swapchain->GetBuffer(0, IID_PPV_ARGS(&texture));
    if (FAILED(hr)) return hr;
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    RECT client{};
    if (!GetClientRect(r.window, &client)) return HRESULT_FROM_WIN32(GetLastError());
    if (client.right <= 0 || client.bottom <= 0) return E_INVALIDARG;
    const UINT width = std::min(description.Width, static_cast<UINT>(client.right));
    const UINT height = std::min(description.Height, static_cast<UINT>(client.bottom));
    const UINT left = (description.Width - width) / 2;
    const D3D11_BOX crop{left, 0, 0, left + width, height, 1};
    description.Width = width; description.Height = height;
    description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ; description.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(hr = r.device->CreateTexture2D(&description, nullptr, &staging))) return hr;
    // Match the composition visual's centered crop into the current HWND.
    r.immediate->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture.Get(), 0, &crop);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(hr = r.immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return hr;
    struct Unmap { ID3D11DeviceContext* context; ID3D11Texture2D* texture; ~Unmap() { context->Unmap(texture, 0); } } unmap{r.immediate.Get(), staging.Get()};
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return hr;
    if (FAILED(hr = wic->CreateStream(&stream))) return hr;
    if (FAILED(hr = stream->InitializeFromFilename(path, GENERIC_WRITE))) return hr;
    if (FAILED(hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) return hr;
    if (FAILED(hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return hr;
    if (FAILED(hr = encoder->CreateNewFrame(&frame, nullptr))) return hr;
    if (FAILED(hr = frame->Initialize(nullptr))) return hr;
    if (FAILED(hr = frame->SetSize(description.Width, description.Height))) return hr;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(hr = frame->SetPixelFormat(&format))) return hr;
    ComPtr<IWICBitmap> bitmap;
    if (FAILED(hr = wic->CreateBitmapFromMemory(description.Width, description.Height, GUID_WICPixelFormat32bppPBGRA,
        mapped.RowPitch, mapped.RowPitch * description.Height, static_cast<BYTE*>(mapped.pData), &bitmap))) return hr;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(hr = wic->CreateFormatConverter(&converter))) return hr;
    if (FAILED(hr = converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0,
                                          WICBitmapPaletteTypeCustom))) return hr;
    if (FAILED(hr = frame->WriteSource(converter.Get(), nullptr))) return hr;
    if (FAILED(hr = frame->Commit())) return hr;
    return encoder->Commit();
}
} // namespace island
