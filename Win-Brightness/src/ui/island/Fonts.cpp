#include "Fonts.h"
#include "../../resources/resources.h"
#include <wrl.h>
#include <array>
#include <cstring>

#pragma comment(lib, "dwrite.lib")

namespace island {
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;

namespace {
class ResourceStream final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDWriteFontFileStream> {
public:
    ResourceStream(HMODULE module, UINT id) {
        const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
        if (resource) {
            m_size = SizeofResource(module, resource);
            m_data = static_cast<const BYTE*>(LockResource(LoadResource(module, resource)));
        }
    }
    HRESULT STDMETHODCALLTYPE ReadFileFragment(const void** start, UINT64 offset, UINT64 size,
                                               void** context) override {
        if (!start || !context) return E_POINTER;
        *start = nullptr;
        *context = nullptr;
        if (!m_data || offset > m_size || size > m_size - offset) return E_FAIL;
        *start = m_data + static_cast<size_t>(offset);
        return S_OK;
    }
    void STDMETHODCALLTYPE ReleaseFileFragment(void*) override {}
    HRESULT STDMETHODCALLTYPE GetFileSize(UINT64* size) override {
        if (!size) return E_POINTER;
        *size = m_size;
        return m_data ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE GetLastWriteTime(UINT64* time) override {
        if (!time) return E_POINTER;
        *time = 0;
        return S_OK;
    }
private:
    const BYTE* m_data = nullptr;
    UINT64 m_size = 0;
};

class ResourceLoader final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDWriteFontFileLoader> {
public:
    explicit ResourceLoader(HMODULE module) : m_module(module) {}
    HRESULT STDMETHODCALLTYPE CreateStreamFromKey(const void* key, UINT32 size,
                                                 IDWriteFontFileStream** stream) override {
        if (!stream) return E_POINTER;
        *stream = nullptr;
        if (!key || size != sizeof(UINT)) return E_INVALIDARG;
        UINT id = 0;
        std::memcpy(&id, key, sizeof(id));
        auto instance = Make<ResourceStream>(m_module, id);
        if (!instance) return E_OUTOFMEMORY;
        UINT64 bytes = 0;
        if (FAILED(instance->GetFileSize(&bytes))) return E_FAIL;
        return instance.CopyTo(stream);
    }
private:
    HMODULE m_module;
};

class FontEnumerator final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDWriteFontFileEnumerator> {
public:
    FontEnumerator(IDWriteFactory* factory, IDWriteFontFileLoader* loader)
        : m_factory(factory), m_loader(loader) {}
    HRESULT STDMETHODCALLTYPE MoveNext(BOOL* hasCurrent) override {
        if (!hasCurrent) return E_POINTER;
        *hasCurrent = FALSE;
        m_current.Reset();
        constexpr std::array<UINT, 3> ids{IDR_INTER_REGULAR, IDR_INTER_MEDIUM, IDR_INTER_SEMIBOLD};
        if (m_index >= ids.size()) return S_OK;
        const UINT key = ids[m_index++];
        const HRESULT result = m_factory->CreateCustomFontFileReference(&key, sizeof(key), m_loader.Get(), &m_current);
        *hasCurrent = SUCCEEDED(result);
        return result;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentFontFile(IDWriteFontFile** file) override {
        return m_current ? m_current.CopyTo(file) : E_FAIL;
    }
private:
    ComPtr<IDWriteFactory> m_factory;
    ComPtr<IDWriteFontFileLoader> m_loader;
    ComPtr<IDWriteFontFile> m_current;
    size_t m_index = 0;
};

class CollectionLoader final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDWriteFontCollectionLoader> {
public:
    explicit CollectionLoader(IDWriteFontFileLoader* loader) : m_loader(loader) {}
    HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(IDWriteFactory* factory, const void*, UINT32,
                                                       IDWriteFontFileEnumerator** enumerator) override {
        auto instance = Make<FontEnumerator>(factory, m_loader.Get());
        return instance ? instance.CopyTo(enumerator) : E_OUTOFMEMORY;
    }
private:
    ComPtr<IDWriteFontFileLoader> m_loader;
};
} // namespace

struct Fonts::Impl {
    ComPtr<IDWriteFactory> factory;
    ComPtr<ResourceLoader> files;
    ComPtr<CollectionLoader> collections;
    ComPtr<IDWriteFontCollection> collection;
    bool filesRegistered = false;
    bool collectionsRegistered = false;

    ~Impl() {
        // Font files are immutable executable resources. No global font install,
        // temporary file, or private GDI font registration is needed.
        collection.Reset();
        if (collectionsRegistered) factory->UnregisterFontCollectionLoader(collections.Get());
        if (filesRegistered) factory->UnregisterFontFileLoader(files.Get());
    }
};

Fonts::Fonts() : m_impl(std::make_unique<Impl>()) {}
Fonts::~Fonts() = default;
bool Fonts::UsesInter() const { return m_impl->collection != nullptr; }

HRESULT Fonts::Initialize(IDWriteFactory* factory, HMODULE module) {
    if (!factory) return E_POINTER;
    m_impl = std::make_unique<Impl>();
    m_impl->factory = factory;
    if (!FindResourceW(module, MAKEINTRESOURCEW(IDR_INTER_REGULAR), RT_RCDATA)) return S_FALSE;
    m_impl->files = Make<ResourceLoader>(module);
    if (!m_impl->files) return E_OUTOFMEMORY;
    HRESULT result = factory->RegisterFontFileLoader(m_impl->files.Get());
    if (FAILED(result)) return result;
    m_impl->filesRegistered = true;
    m_impl->collections = Make<CollectionLoader>(m_impl->files.Get());
    if (!m_impl->collections) return E_OUTOFMEMORY;
    result = factory->RegisterFontCollectionLoader(m_impl->collections.Get());
    if (FAILED(result)) return result;
    m_impl->collectionsRegistered = true;
    constexpr UINT key = 1;
    result = factory->CreateCustomFontCollection(m_impl->collections.Get(), &key, sizeof(key), &m_impl->collection);
    if (FAILED(result)) m_impl->collection.Reset();
    return result;
}

HRESULT Fonts::CreateFormat(float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** format) const {
    if (!m_impl->factory) return E_UNEXPECTED;
    return m_impl->factory->CreateTextFormat(UsesInter() ? L"Inter" : L"Segoe UI", m_impl->collection.Get(),
        weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-US", format);
}
} // namespace island
