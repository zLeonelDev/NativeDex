#pragma once

#include <windows.h>
#include <string>
#include <functional>
#include <memory>

namespace NativeDex::Platform::Windows {

class Win32Window {
public:
    using MessageCallback = std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)>;
    
    struct Config {
        std::wstring title = L"NativeDex";
        int width = 800;
        int height = 600;
        bool fullscreen = false;
        bool resizable = true;
        HICON icon = nullptr;
        HCURSOR cursor = LoadCursor(nullptr, IDC_ARROW);
        HBRUSH background = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    };

    Win32Window();
    ~Win32Window();

    bool create(const Config& config);
    void destroy();
    
    void show(int cmdShow = SW_SHOW);
    void hide();
    void setTitle(const std::wstring& title);
    void setSize(int width, int height);
    void getSize(int& width, int& height) const;
    void getClientSize(int& width, int& height) const;
    
    HWND getHandle() const { return hwnd_; }
    bool isValid() const { return hwnd_ != nullptr; }

    // Force a repaint (WM_PAINT) of the window
    void invalidate(bool eraseBackground = false);
    
    // Message loop
    int runMessageLoop();
    void postQuit(int exitCode = 0);
    
    // Event callbacks
    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setPaintCallback(std::function<void(HDC)> cb) { paintCallback_ = std::move(cb); }
    void setResizeCallback(std::function<void(int, int)> cb) { resizeCallback_ = std::move(cb); }
    void setKeyboardCallback(std::function<void(UINT, WPARAM, LPARAM)> cb) { keyboardCallback_ = std::move(cb); }
    void setMouseCallback(std::function<void(UINT, WPARAM, LPARAM)> cb) { mouseCallback_ = std::move(cb); }

private:
    static LRESULT CALLBACK staticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    
    void registerWindowClass();
    void unregisterWindowClass();
    
    HWND hwnd_ = nullptr;
    Config config_;
    MessageCallback messageCallback_;
    std::function<void(HDC)> paintCallback_;
    std::function<void(int, int)> resizeCallback_;
    std::function<void(UINT, WPARAM, LPARAM)> keyboardCallback_;
    std::function<void(UINT, WPARAM, LPARAM)> mouseCallback_;
    
    static constexpr wchar_t CLASS_NAME[] = L"NativeDexWindowClass";
    static bool classRegistered_;
};

} // namespace NativeDex::Platform::Windows