#include "Direct2DGraphics.h"
#include <d2d1helper.h>
#include <wincodec.h>
#include <iostream>

namespace NativeDex::Platform::Windows {

Direct2DGraphics::Direct2DGraphics() = default;

Direct2DGraphics::~Direct2DGraphics() {
    shutdown();
}

bool Direct2DGraphics::initialize(const Config& config) {
    config_ = config;
    
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "[Direct2D] Failed to create D2D factory: " << std::hex << hr << std::endl;
        return false;
    }
    
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), 
                            reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf()));
    if (FAILED(hr)) {
        std::cerr << "[Direct2D] Failed to create DWrite factory: " << std::hex << hr << std::endl;
        return false;
    }
    
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&wicFactory_));
    if (FAILED(hr)) {
        std::cerr << "[Direct2D] Failed to create WIC factory: " << std::hex << hr << std::endl;
        return false;
    }
    
    hr = createDeviceResources();
    if (FAILED(hr)) {
        std::cerr << "[Direct2D] Failed to create device resources: " << std::hex << hr << std::endl;
        return false;
    }
    
    createCommonResources();
    return true;
}

void Direct2DGraphics::shutdown() {
    discardDeviceResources();
    renderTarget_.Reset();
    defaultBrush_.Reset();
    writeFactory_.Reset();
    wicFactory_.Reset();
    factory_.Reset();
}

HRESULT Direct2DGraphics::createDeviceResources() {
    if (renderTarget_) return S_OK;
    
    RECT rc;
    GetClientRect(config_.hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    rtProps.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    rtProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    rtProps.usage = D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE;
    
    HRESULT hr = factory_->CreateHwndRenderTarget(
        rtProps,
        D2D1::HwndRenderTargetProperties(config_.hwnd, size),
        &renderTarget_
    );
    
    if (SUCCEEDED(hr)) {
        renderTarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        renderTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    }
    
    return hr;
}

void Direct2DGraphics::discardDeviceResources() {
    renderTarget_.Reset();
    defaultBrush_.Reset();
}

void Direct2DGraphics::createCommonResources() {
    if (renderTarget_) {
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &defaultBrush_);
    }
}

D2D1_RECT_F Direct2DGraphics::toD2DRect(const Rect& rect) const {
    return D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom);
}

void Direct2DGraphics::beginDraw() {
    HRESULT hr = createDeviceResources();
    if (FAILED(hr)) return;
    
    renderTarget_->BeginDraw();
}

void Direct2DGraphics::endDraw() {
    if (!renderTarget_) return;
    
    HRESULT hr = renderTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        discardDeviceResources();
    }
}

void Direct2DGraphics::clear(const Color& color) {
    if (!renderTarget_) return;
    renderTarget_->Clear(D2D1::ColorF(color.r, color.g, color.b, color.a));
}

void Direct2DGraphics::drawRect(const Rect& rect, const Color& color, float strokeWidth) {
    if (!renderTarget_) return;
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);
    renderTarget_->DrawRectangle(toD2DRect(rect), brush.Get(), strokeWidth);
}

void Direct2DGraphics::fillRect(const Rect& rect, const Color& color) {
    if (!renderTarget_) return;
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);
    renderTarget_->FillRectangle(toD2DRect(rect), brush.Get());
}

void Direct2DGraphics::drawRoundedRect(const Rect& rect, float radiusX, float radiusY, 
                                       const Color& color, float strokeWidth) {
    if (!renderTarget_) return;
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);
    
    D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(toD2DRect(rect), radiusX, radiusY);
    renderTarget_->DrawRoundedRectangle(roundedRect, brush.Get(), strokeWidth);
}

void Direct2DGraphics::fillRoundedRect(const Rect& rect, float radiusX, float radiusY, const Color& color) {
    if (!renderTarget_) return;
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);
    
    D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(toD2DRect(rect), radiusX, radiusY);
    renderTarget_->FillRoundedRectangle(roundedRect, brush.Get());
}

void Direct2DGraphics::drawLine(const Point& p1, const Point& p2, const Color& color, float strokeWidth) {
    if (!renderTarget_) return;
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);
    renderTarget_->DrawLine(D2D1::Point2F(p1.x, p1.y), D2D1::Point2F(p2.x, p2.y), brush.Get(), strokeWidth);
}

void Direct2DGraphics::drawLines(const std::vector<Point>& points, const Color& color, float strokeWidth) {
    if (!renderTarget_ || points.size() < 2) return;
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);
    
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> pathGeometry;
    factory_->CreatePathGeometry(&pathGeometry);
    
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    pathGeometry->Open(&sink);
    
    sink->BeginFigure(D2D1::Point2F(points[0].x, points[0].y), D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < points.size(); ++i) {
        sink->AddLine(D2D1::Point2F(points[i].x, points[i].y));
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    
    renderTarget_->DrawGeometry(pathGeometry.Get(), brush.Get(), strokeWidth);
}

void Direct2DGraphics::drawEllipse(const Point& center, float radiusX, float radiusY, 
                                   const Color& color, float strokeWidth) {
    if (!renderTarget_) return;
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);
    renderTarget_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(center.x, center.y), radiusX, radiusY), 
                              brush.Get(), strokeWidth);
}

void Direct2DGraphics::fillEllipse(const Point& center, float radiusX, float radiusY, const Color& color) {
    if (!renderTarget_) return;
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);
    renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center.x, center.y), radiusX, radiusY), brush.Get());
}

void Direct2DGraphics::drawText(const std::wstring& text, const Rect& layoutRect, const Color& color,
                                float fontSize, const std::wstring& fontFamily,
                                bool bold, bool italic,
                                DWRITE_TEXT_ALIGNMENT alignment,
                                DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment) {
    if (!renderTarget_ || !writeFactory_) return;
    
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat;
    writeFactory_->CreateTextFormat(
        fontFamily.c_str(), nullptr,
        bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"", &textFormat
    );
    
    textFormat->SetTextAlignment(alignment);
    textFormat->SetParagraphAlignment(paragraphAlignment);
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);
    
    D2D1_RECT_F layout = D2D1::RectF(layoutRect.left, layoutRect.top, layoutRect.right, layoutRect.bottom);
    renderTarget_->DrawText(text.c_str(), static_cast<UINT32>(text.length()), textFormat.Get(), layout, brush.Get());
}

Direct2DGraphics::Size Direct2DGraphics::measureText(const std::wstring& text, float fontSize,
                                                      const std::wstring& fontFamily, bool bold, bool italic) {
    Size size{0, 0};
    if (!writeFactory_) return size;
    
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat;
    writeFactory_->CreateTextFormat(
        fontFamily.c_str(), nullptr,
        bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"", &textFormat
    );
    
    Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
    writeFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.length()), 
                                   textFormat.Get(), FLT_MAX, FLT_MAX, &textLayout);
    
    DWRITE_TEXT_METRICS metrics;
    textLayout->GetMetrics(&metrics);
    size.width = metrics.width;
    size.height = metrics.height;
    
    return size;
}

std::shared_ptr<Direct2DGraphics::Bitmap> Direct2DGraphics::createBitmapFromFile(const std::wstring& path) {
    if (!wicFactory_ || !renderTarget_) return nullptr;
    
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wicFactory_->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder
    );
    if (FAILED(hr)) return nullptr;
    
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return nullptr;
    
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = wicFactory_->CreateFormatConverter(&converter);
    if (FAILED(hr)) return nullptr;
    
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, 
                              WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) return nullptr;
    
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    hr = renderTarget_->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bitmap);
    if (FAILED(hr)) return nullptr;
    
    class WicBitmap : public Bitmap {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap_;
    public:
        WicBitmap(ID2D1Bitmap* bmp) : bitmap_(bmp) {}
        Size getSize() const override {
            D2D1_SIZE_F s = bitmap_->GetSize();
            return Size{s.width, s.height};
        }
        ID2D1Bitmap* getD2DBitmap() override { return bitmap_.Get(); }
    };
    
    return std::make_shared<WicBitmap>(bitmap.Get());
}

std::shared_ptr<Direct2DGraphics::Bitmap> Direct2DGraphics::createBitmap(int width, int height) {
    if (!renderTarget_) return nullptr;
    
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    D2D1_SIZE_U size = D2D1::SizeU(width, height);
    HRESULT hr = renderTarget_->CreateBitmap(size, nullptr, 0, 
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &bitmap);
    if (FAILED(hr)) return nullptr;
    
    class D2DBitmap : public Bitmap {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap_;
    public:
        D2DBitmap(ID2D1Bitmap* bmp) : bitmap_(bmp) {}
        Size getSize() const override {
            D2D1_SIZE_F s = bitmap_->GetSize();
            return Size{s.width, s.height};
        }
        ID2D1Bitmap* getD2DBitmap() override { return bitmap_.Get(); }
    };
    
    return std::make_shared<D2DBitmap>(bitmap.Get());
}

void Direct2DGraphics::drawBitmap(Bitmap* bitmap, const Rect& destRect, const Rect* srcRect, float opacity) {
    if (!renderTarget_ || !bitmap) return;
    
    ID2D1Bitmap* d2dBitmap = bitmap->getD2DBitmap();
    if (!d2dBitmap) return;
    
    D2D1_RECT_F dest = toD2DRect(destRect);
    D2D1_RECT_F* src = nullptr;
    D2D1_RECT_F srcRectD2D;
    if (srcRect) {
        srcRectD2D = toD2DRect(*srcRect);
        src = &srcRectD2D;
    }
    
    renderTarget_->DrawBitmap(d2dBitmap, dest, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
}

void Direct2DGraphics::drawBitmap(Bitmap* bitmap, const Point& destPoint, const Rect* srcRect, float opacity) {
    if (!renderTarget_ || !bitmap) return;
    
    ID2D1Bitmap* d2dBitmap = bitmap->getD2DBitmap();
    if (!d2dBitmap) return;
    
    D2D1_SIZE_F size = d2dBitmap->GetSize();
    Rect destRect(destPoint.x, destPoint.y, destPoint.x + size.width, destPoint.y + size.height);
    drawBitmap(bitmap, destRect, srcRect, opacity);
}

void Direct2DGraphics::pushTransform() {
    if (renderTarget_) {
        D2D1_MATRIX_3X2_F current;
        renderTarget_->GetTransform(&current);
        transformStack_.push_back(current);
    }
}

void Direct2DGraphics::popTransform() {
    if (!transformStack_.empty() && renderTarget_) {
        renderTarget_->SetTransform(transformStack_.back());
        transformStack_.pop_back();
    }
}

void Direct2DGraphics::translate(float x, float y) {
    if (renderTarget_) {
        D2D1_MATRIX_3X2_F current;
        renderTarget_->GetTransform(&current);
        renderTarget_->SetTransform(D2D1::Matrix3x2F::Translation(x, y) * current);
    }
}

void Direct2DGraphics::rotate(float angle) {
    if (renderTarget_) {
        D2D1_MATRIX_3X2_F current;
        renderTarget_->GetTransform(&current);
        renderTarget_->SetTransform(D2D1::Matrix3x2F::Rotation(angle) * current);
    }
}

void Direct2DGraphics::scale(float x, float y) {
    if (renderTarget_) {
        D2D1_MATRIX_3X2_F current;
        renderTarget_->GetTransform(&current);
        renderTarget_->SetTransform(D2D1::Matrix3x2F::Scale(x, y) * current);
    }
}

void Direct2DGraphics::setTransform(const D2D1_MATRIX_3X2_F& transform) {
    if (renderTarget_) {
        renderTarget_->SetTransform(transform);
    }
}

void Direct2DGraphics::pushClip(const Rect& rect) {
    if (renderTarget_) {
        D2D1_RECT_F clipRect = toD2DRect(rect);
        renderTarget_->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        clipStack_.push_back(clipRect);
    }
}

void Direct2DGraphics::popClip() {
    if (!clipStack_.empty() && renderTarget_) {
        renderTarget_->PopAxisAlignedClip();
        clipStack_.pop_back();
    }
}

void Direct2DGraphics::resize(int width, int height) {
    config_.width = width;
    config_.height = height;
    
    if (renderTarget_) {
        D2D1_SIZE_U size = D2D1::SizeU(width, height);
        HRESULT hr = renderTarget_->Resize(size);
        if (hr == D2DERR_RECREATE_TARGET) {
            discardDeviceResources();
        }
    }
}

} // namespace NativeDex::Platform::Windows