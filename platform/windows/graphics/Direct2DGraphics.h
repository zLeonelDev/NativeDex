#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <memory>

namespace NativeDex::Platform::Windows {

class Direct2DGraphics {
public:
    struct Config {
        HWND hwnd = nullptr;
        int width = 800;
        int height = 600;
        bool enableVSync = true;
    };
    
    struct Color {
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
        
        Color() = default;
        Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
        
        static Color fromArgb(uint32_t argb) {
            return Color(
                ((argb >> 16) & 0xFF) / 255.0f,
                ((argb >> 8) & 0xFF) / 255.0f,
                (argb & 0xFF) / 255.0f,
                ((argb >> 24) & 0xFF) / 255.0f
            );
        }
        
        uint32_t toArgb() const {
            return (static_cast<uint32_t>(a * 255) << 24) |
                   (static_cast<uint32_t>(r * 255) << 16) |
                   (static_cast<uint32_t>(g * 255) << 8) |
                   static_cast<uint32_t>(b * 255);
        }
    };
    
    struct Rect {
        float left = 0, top = 0, right = 0, bottom = 0;
        
        Rect() = default;
        Rect(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}
        
        float width() const { return right - left; }
        float height() const { return bottom - top; }
    };
    
    struct Point {
        float x = 0, y = 0;
    };
    
    struct Size {
        float width = 0, height = 0;
    };

    Direct2DGraphics();
    ~Direct2DGraphics();
    
    bool initialize(const Config& config);
    void shutdown();
    
    // Rendering
    void beginDraw();
    void endDraw();
    void clear(const Color& color = Color(1, 1, 1, 1));
    
    // Drawing
    void drawRect(const Rect& rect, const Color& color, float strokeWidth = 1.0f);
    void fillRect(const Rect& rect, const Color& color);
    void drawRoundedRect(const Rect& rect, float radiusX, float radiusY, const Color& color, float strokeWidth = 1.0f);
    void fillRoundedRect(const Rect& rect, float radiusX, float radiusY, const Color& color);
    
    void drawLine(const Point& p1, const Point& p2, const Color& color, float strokeWidth = 1.0f);
    void drawLines(const std::vector<Point>& points, const Color& color, float strokeWidth = 1.0f);
    
    void drawEllipse(const Point& center, float radiusX, float radiusY, const Color& color, float strokeWidth = 1.0f);
    void fillEllipse(const Point& center, float radiusX, float radiusY, const Color& color);
    
    // Text
    void drawText(const std::wstring& text, const Rect& layoutRect, const Color& color, 
                  float fontSize = 14.0f, const std::wstring& fontFamily = L"Segoe UI",
                  bool bold = false, bool italic = false,
                  DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment = DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    
    Size measureText(const std::wstring& text, float fontSize = 14.0f, 
                     const std::wstring& fontFamily = L"Segoe UI", bool bold = false, bool italic = false);
    
    // Bitmap
    class Bitmap {
    public:
        virtual ~Bitmap() = default;
        virtual Size getSize() const = 0;
        virtual ID2D1Bitmap* getD2DBitmap() = 0;
    };
    
    std::shared_ptr<Bitmap> createBitmapFromFile(const std::wstring& path);
    std::shared_ptr<Bitmap> createBitmapFromMemory(const uint8_t* data, size_t size, int width, int height, int stride);
    std::shared_ptr<Bitmap> createBitmap(int width, int height);
    
    void drawBitmap(Bitmap* bitmap, const Rect& destRect, const Rect* srcRect = nullptr, float opacity = 1.0f);
    void drawBitmap(Bitmap* bitmap, const Point& destPoint, const Rect* srcRect = nullptr, float opacity = 1.0f);
    
    // Transforms
    void pushTransform();
    void popTransform();
    void translate(float x, float y);
    void rotate(float angle);
    void scale(float x, float y);
    void setTransform(const D2D1_MATRIX_3X2_F& transform);
    
    // Clipping
    void pushClip(const Rect& rect);
    void popClip();
    
    // Resize
    void resize(int width, int height);
    
    // Access to D2D objects (for advanced usage)
    ID2D1Factory* getFactory() const { return factory_.Get(); }
    ID2D1HwndRenderTarget* getRenderTarget() const { return renderTarget_.Get(); }
    IDWriteFactory* getWriteFactory() const { return writeFactory_.Get(); }
    IWICImagingFactory* getWicFactory() const { return wicFactory_.Get(); }
    
    bool isValid() const { return renderTarget_ != nullptr; }
    int getWidth() const { return config_.width; }
    int getHeight() const { return config_.height; }

private:
    Config config_;
    
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory_;
    
    // Common brushes (cached)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> defaultBrush_;
    
    // Transform stack
    std::vector<D2D1_MATRIX_3X2_F> transformStack_;
    
    // Clip stack
    std::vector<D2D1_RECT_F> clipStack_;
    
    HRESULT createDeviceResources();
    void discardDeviceResources();
    void createCommonResources();
    D2D1_RECT_F toD2DRect(const Rect& rect) const;
};

} // namespace NativeDex::Platform::Windows