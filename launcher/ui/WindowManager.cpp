#include "WindowManager.h"
#include <iostream>

namespace NativeDex::Launcher {

WindowManager::WindowManager() = default;

WindowManager::~WindowManager() {
    shutdown();
}

bool WindowManager::initialize(const std::string& title, const std::string& splashText) {
    // Create Win32 window
    NativeDex::Platform::Windows::Win32Window::Config config;
    config.title = std::wstring(title.begin(), title.end());
    config.width = 800;
    config.height = 600;
    config.resizable = true;
    
    window_ = std::make_unique<NativeDex::Platform::Windows::Win32Window>();
    if (!window_->create(config)) {
        std::cerr << "[WindowManager] Failed to create window" << std::endl;
        return false;
    }
    
    // Initialize Direct2D graphics
    NativeDex::Platform::Windows::Direct2DGraphics::Config gfxConfig;
    gfxConfig.hwnd = window_->getHandle();
    gfxConfig.width = 800;
    gfxConfig.height = 600;
    
    graphics_ = std::make_unique<NativeDex::Platform::Windows::Direct2DGraphics>();
    if (!graphics_->initialize(gfxConfig)) {
        std::cerr << "[WindowManager] Failed to initialize Direct2D" << std::endl;
        return false;
    }
    
    // Set up callbacks
    window_->setPaintCallback([this](HDC) { onPaint(); });
    window_->setResizeCallback([this](int w, int h) { onResize(w, h); });
    
    // Show window
    window_->show(SW_SHOW);
    
    return true;
}

void WindowManager::shutdown() {
    graphics_.reset();
    window_.reset();
}

void WindowManager::setTitle(const std::string& title) {
    std::wstring wtitle(title.begin(), title.end());
    if (window_) window_->setTitle(wtitle);
}

void WindowManager::setSplashText(const std::string& text) {
    // Would render splash text
    if (paintCallback_) {
        requestRedraw();
    }
}

void WindowManager::setPaintCallback(PaintCallback cb) {
    paintCallback_ = std::move(cb);
}

void WindowManager::setResizeCallback(ResizeCallback cb) {
    resizeCallback_ = std::move(cb);
}

void WindowManager::onPaint() {
    if (!graphics_) return;
    
    graphics_->beginDraw();
    graphics_->clear();
    
    if (paintCallback_) {
        // For GDI compatibility, we don't pass HDC
        // Just let the callback know to use Direct2D
    }
    
    // Custom drawing would go here
    // For splash screen, draw text
    
    graphics_->endDraw();
}

void WindowManager::onResize(int width, int height) {
    if (graphics_) {
        graphics_->resize(width, height);
    }
    if (resizeCallback_) {
        resizeCallback_(width, height);
    }
}

int WindowManager::runMessageLoop() {
    return window_ ? window_->runMessageLoop() : 0;
}

void WindowManager::requestRedraw() {
    if (window_) {
        InvalidateRect(window_->getHandle(), nullptr, FALSE);
    }
}

void WindowManager::getClientSize(int& width, int& height) const {
    if (window_) {
        window_->getClientSize(width, height);
    } else {
        width = 800;
        height = 600;
    }
}

} // namespace NativeDex::Launcher