#pragma once

#include <string>
#include <functional>
#include "../../platform/windows/window/Win32Window.h"
#include "../../platform/windows/graphics/Direct2DGraphics.h"

namespace NativeDex::Launcher {

class WindowManager {
public:
    using PaintCallback = std::function<void(HDC)>;
    using ResizeCallback = std::function<void(int, int)>;
    
    WindowManager();
    ~WindowManager();
    
    bool initialize(const std::string& title, const std::string& splashText = "");
    void shutdown();
    
    void setTitle(const std::string& title);
    void setSplashText(const std::string& text);
    
    void setPaintCallback(PaintCallback cb);
    void setResizeCallback(ResizeCallback cb);
    
    int runMessageLoop();
    void requestRedraw();
    
    // Win32 window handle
    HWND getHandle() const { return window_ ? window_->getHandle() : nullptr; }
    
    // Size
    void getClientSize(int& width, int& height) const;
    
private:
    std::unique_ptr<NativeDex::Platform::Windows::Win32Window> window_;
    std::unique_ptr<NativeDex::Platform::Windows::Direct2DGraphics> graphics_;
    
    PaintCallback paintCallback_;
    ResizeCallback resizeCallback_;
    
    void onPaint();
    void onResize(int width, int height);
};

} // namespace NativeDex::Launcher