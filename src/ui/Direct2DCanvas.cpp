#include "ui/Direct2DCanvas.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace ghovlz {
namespace {

D2D1_COLOR_F toD2d(Color c) {
    return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f);
}

std::wstring widen(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

} // namespace

class Direct2DCanvas final : public Canvas {
public:
    Direct2DCanvas(HWND hwnd, int w, int h) : hwnd_(hwnd), w_(w), h_(h) { ready_ = create(); }

    int width() const override { return std::max(1, w_ * 96 / std::max(96, dpi_)); }
    int height() const override { return std::max(1, h_ * 96 / std::max(96, dpi_)); }
    const char* backendName() const override { return warp_ ? "WARP" : (ready_ ? "D3D11" : "CPU"); }
    bool ready() const { return ready_; }
    void setDpi(int dpi) { applyDpi(dpi); }

    void resize(int w, int h) {
        if (!ready_ || !swap_ || w < 1 || h < 1 || (w == w_ && h == h_))
            return;
        w_ = w;
        h_ = h;
        target_.Reset();
        dc_->SetTarget(nullptr);
        if (FAILED(swap_->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, 0))) {
            ready_ = create();
            return;
        }
        ready_ = bindTarget();
    }

    void begin() override {
        if (!dc_)
            return;
        dc_->BeginDraw();
        dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    void end() override {
        if (!dc_ || !swap_)
            return;
        const HRESULT drawn = dc_->EndDraw();
        const HRESULT presented = swap_->Present(1, 0);
        if (drawn == D2DERR_RECREATE_TARGET || presented == DXGI_ERROR_DEVICE_REMOVED || presented == DXGI_ERROR_DEVICE_RESET)
            ready_ = create();
    }

    void clear(Color color) override {
        if (dc_)
            dc_->Clear(toD2d(color));
    }
    void fillRect(Rect r, Color color) override {
        auto brush = fill(color);
        if (brush)
            dc_->FillRectangle(D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h), brush.Get());
    }
    void strokeRect(Rect r, Color color, float thickness) override {
        auto brush = fill(color);
        if (brush)
            dc_->DrawRectangle(D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h), brush.Get(), thickness);
    }
    void fillRoundRect(Rect r, float radius, Color color) override {
        auto brush = fill(color);
        if (!brush)
            return;
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h), radius, radius);
        dc_->FillRoundedRectangle(rr, brush.Get());
    }
    void fillEllipse(float cx, float cy, float rx, float ry, Color color) override {
        auto brush = fill(color);
        if (brush)
            dc_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), brush.Get());
    }
    void line(float x0, float y0, float x1, float y1, Color color, float thickness) override {
        auto brush = fill(color);
        if (brush)
            dc_->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), brush.Get(), thickness);
    }
    void arc(float cx, float cy, float radius, float a0, float a1, Color color, float thickness) override {
        if (!factory_ || !dc_ || a1 <= a0)
            return;
        ComPtr<ID2D1PathGeometry> geo;
        if (FAILED(factory_->CreatePathGeometry(&geo)))
            return;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geo->Open(&sink)))
            return;
        const D2D1_POINT_2F start{cx + std::cos(a0) * radius, cy + std::sin(a0) * radius};
        const D2D1_POINT_2F end{cx + std::cos(a1) * radius, cy + std::sin(a1) * radius};
        sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
        D2D1_ARC_SEGMENT seg{};
        seg.point = end;
        seg.size = D2D1::SizeF(radius, radius);
        seg.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
        seg.arcSize = (a1 - a0) > 3.14159265f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
        sink->AddArc(seg);
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        auto brush = fill(color);
        if (brush)
            dc_->DrawGeometry(geo.Get(), brush.Get(), thickness);
    }
    void clip(Rect r) override {
        if (!dc_)
            return;
        dc_->PushAxisAlignedClip(D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ++clipDepth_;
    }
    void resetClip() override {
        if (!dc_)
            return;
        while (clipDepth_ > 0) {
            dc_->PopAxisAlignedClip();
            --clipDepth_;
        }
    }
    void drawText(const std::string& text, float x, float y, float size, Color color, bool bold) override {
        if (!dc_ || !write_)
            return;
        ComPtr<IDWriteTextFormat> format;
        const wchar_t* face = size >= 22.f ? L"Bahnschrift" : L"Segoe UI";
        write_->CreateTextFormat(face, nullptr, bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &format);
        if (!format)
            return;
        auto brush = fill(color);
        const std::wstring wide = widen(text);
        dc_->DrawTextW(wide.c_str(), static_cast<UINT32>(wide.size()), format.Get(),
                       D2D1::RectF(x, y, x + 2000.f, y + size * 2.f), brush.Get());
    }
    float textWidth(const std::string& text, float size, bool bold) const override {
        if (!write_)
            return text.size() * size * 0.5f;
        ComPtr<IDWriteTextFormat> format;
        write_->CreateTextFormat(L"Segoe UI", nullptr, bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &format);
        ComPtr<IDWriteTextLayout> layout;
        const std::wstring wide = widen(text);
        write_->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()), format.Get(), 4000.f, size * 2.f, &layout);
        if (!layout)
            return text.size() * size * 0.5f;
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        return metrics.width;
    }
    void dropShadow(Rect r, float radius, float blur, Color color) override {
        if (!dc_)
            return;
        const Key key{static_cast<int>(r.w), static_cast<int>(r.h), static_cast<int>(blur), static_cast<int>(radius)};
        auto it = shadows_.find(key);
        if (it == shadows_.end()) {
            ComPtr<ID2D1BitmapRenderTarget> rt;
            D2D1_SIZE_F sz = D2D1::SizeF(std::max(1.f, r.w), std::max(1.f, r.h));
            if (FAILED(dc_->CreateCompatibleRenderTarget(sz, &rt))) {
                fillRoundRect({r.x, r.y + 4, r.w, r.h}, radius, color);
                return;
            }
            rt->BeginDraw();
            rt->Clear(D2D1::ColorF(0, 0, 0, 0));
            ComPtr<ID2D1SolidColorBrush> white;
            rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &white);
            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(0, 0, r.w, r.h), radius, radius);
            rt->FillRoundedRectangle(rr, white.Get());
            rt->EndDraw();
            ComPtr<ID2D1Bitmap> bmp;
            rt->GetBitmap(&bmp);
            ComPtr<ID2D1Effect> effect;
            if (FAILED(dc_->CreateEffect(CLSID_D2D1Shadow, &effect))) {
                fillRoundRect({r.x, r.y + 4, r.w, r.h}, radius, color);
                return;
            }
            effect->SetInput(0, bmp.Get());
            const float deviation = std::max(0.5f, blur);
            effect->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, D2D1_PROPERTY_TYPE_FLOAT,
                             reinterpret_cast<const BYTE*>(&deviation), sizeof(deviation));
            const float rgba[4] = {color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f};
            effect->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1_PROPERTY_TYPE_VECTOR4, reinterpret_cast<const BYTE*>(rgba), sizeof(rgba));
            it = shadows_.emplace(key, effect).first;
        }
        dc_->DrawImage(it->second.Get(), D2D1::Point2F(r.x, r.y + 3.f));
    }
    void blurRect(const std::string& cacheKey, Rect r, float sigma) override {
        if (!dc_)
            return;
        auto it = blurs_.find(cacheKey);
        if (it == blurs_.end()) {
            ComPtr<ID2D1Bitmap> src;
            if (FAILED(dc_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(std::max(1.f, r.w)), static_cast<UINT32>(std::max(1.f, r.h))),
                                         nullptr, 0,
                                         D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                         &src)))
                return;
            D2D1_POINT_2U dest{0, 0};
            D2D1_RECT_U srcRect{static_cast<UINT32>(std::max(0.f, r.x)), static_cast<UINT32>(std::max(0.f, r.y)),
                                static_cast<UINT32>(std::max(0.f, r.x + r.w)), static_cast<UINT32>(std::max(0.f, r.y + r.h))};
            src->CopyFromRenderTarget(&dest, dc_.Get(), &srcRect);
            ComPtr<ID2D1Effect> effect;
            if (FAILED(dc_->CreateEffect(CLSID_D2D1GaussianBlur, &effect)))
                return;
            effect->SetInput(0, src.Get());
            const float deviation = std::max(0.5f, sigma);
            effect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, D2D1_PROPERTY_TYPE_FLOAT,
                             reinterpret_cast<const BYTE*>(&deviation), sizeof(deviation));
            it = blurs_.emplace(cacheKey, effect).first;
        }
        dc_->DrawImage(it->second.Get(), D2D1::Point2F(r.x, r.y));
    }
    void drawImage(const Image& image, Rect srcRect, Rect dst) override {
        if (!dc_ || image.w < 1 || image.h < 1)
            return;
        std::vector<uint8_t> bgra(static_cast<size_t>(image.w * image.h * 4));
        for (int i = 0; i < image.w * image.h; ++i) {
            const uint8_t r = image.rgba[static_cast<size_t>(i) * 4];
            const uint8_t g = image.rgba[static_cast<size_t>(i) * 4 + 1];
            const uint8_t b = image.rgba[static_cast<size_t>(i) * 4 + 2];
            const uint8_t a = image.rgba[static_cast<size_t>(i) * 4 + 3];
            bgra[static_cast<size_t>(i) * 4] = static_cast<uint8_t>(b * a / 255);
            bgra[static_cast<size_t>(i) * 4 + 1] = static_cast<uint8_t>(g * a / 255);
            bgra[static_cast<size_t>(i) * 4 + 2] = static_cast<uint8_t>(r * a / 255);
            bgra[static_cast<size_t>(i) * 4 + 3] = a;
        }
        ComPtr<ID2D1Bitmap> bmp;
        dc_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(image.w), static_cast<UINT32>(image.h)), bgra.data(),
                          static_cast<UINT32>(image.w * 4),
                          D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                          &bmp);
        if (!bmp)
            return;
        dc_->DrawBitmap(bmp.Get(), D2D1::RectF(dst.x, dst.y, dst.x + dst.w, dst.y + dst.h), 1.f,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                        D2D1::RectF(srcRect.x, srcRect.y, srcRect.x + srcRect.w, srcRect.y + srcRect.h));
    }
    bool savePng(const std::string&) override { return false; }

private:
    struct Key {
        int w, h, blur, rad;
        bool operator<(const Key& o) const {
            if (w != o.w) return w < o.w;
            if (h != o.h) return h < o.h;
            if (blur != o.blur) return blur < o.blur;
            return rad < o.rad;
        }
    };

    ComPtr<ID2D1SolidColorBrush> fill(Color color) {
        ComPtr<ID2D1SolidColorBrush> brush;
        if (dc_)
            dc_->CreateSolidColorBrush(toD2d(color), &brush);
        return brush;
    }

    bool create() {
        ready_ = false;
        warp_ = false;
        shadows_.clear();
        blurs_.clear();
        target_.Reset();
        dc_.Reset();
        d2dDevice_.Reset();
        swap_.Reset();
        context_.Reset();
        device_.Reset();
        D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2, D3D11_SDK_VERSION,
                                       &device_, &got, &context_);
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2, D3D11_SDK_VERSION, &device_, &got, &context_);
            warp_ = SUCCEEDED(hr);
        }
        if (FAILED(hr))
            return false;
        ComPtr<IDXGIDevice> dxgi;
        device_.As(&dxgi);
        ComPtr<IDXGIAdapter> adapter;
        dxgi->GetAdapter(&adapter);
        ComPtr<IDXGIFactory2> factory;
        adapter->GetParent(IID_PPV_ARGS(&factory));
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = static_cast<UINT>(std::max(1, w_));
        desc.Height = static_cast<UINT>(std::max(1, h_));
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        if (FAILED(factory->CreateSwapChainForHwnd(device_.Get(), hwnd_, &desc, nullptr, nullptr, &swap_)))
            return false;
        factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
        if (!factory_) {
            D2D1_FACTORY_OPTIONS options{};
            if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
                                        reinterpret_cast<void**>(factory_.GetAddressOf()))))
                return false;
        }
        if (FAILED(factory_->CreateDevice(dxgi.Get(), &d2dDevice_)))
            return false;
        if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_)))
            return false;
        if (!write_)
            DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(write_.GetAddressOf()));
        UINT dpi = 96;
        if (auto getDpi = reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")))
            dpi = getDpi(hwnd_);
        applyDpi(static_cast<int>(dpi));
        return bindTarget();
    }

    void applyDpi(int dpi) {
        dpi_ = dpi < 96 ? 96 : dpi;
        if (dc_)
            dc_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    }

    bool bindTarget() {
        ComPtr<IDXGISurface> surface;
        if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&surface))))
            return false;
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        target_.Reset();
        if (FAILED(dc_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target_)))
            return false;
        dc_->SetTarget(target_.Get());
        return true;
    }

    HWND hwnd_ = nullptr;
    int w_ = 0;
    int h_ = 0;
    int dpi_ = 96;
    bool ready_ = false;
    bool warp_ = false;
    int clipDepth_ = 0;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID2D1Factory1> factory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> dc_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<IDWriteFactory> write_;
    std::map<Key, ComPtr<ID2D1Effect>> shadows_;
    std::map<std::string, ComPtr<ID2D1Effect>> blurs_;
};

std::unique_ptr<Canvas> makeDirect2DCanvas(void* hwnd, int width, int height) {
    auto canvas = std::make_unique<Direct2DCanvas>(static_cast<HWND>(hwnd), width, height);
    if (!canvas->ready())
        return nullptr;
    return canvas;
}

void resizeDirect2DCanvas(Canvas& canvas, int width, int height) {
    if (auto* d2d = dynamic_cast<Direct2DCanvas*>(&canvas))
        d2d->resize(width, height);
}

void setDirect2DDpi(Canvas& canvas, int dpi) {
    if (auto* d2d = dynamic_cast<Direct2DCanvas*>(&canvas))
        d2d->setDpi(dpi);
}

} // namespace ghovlz
