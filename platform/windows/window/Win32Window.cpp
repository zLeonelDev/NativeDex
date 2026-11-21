#include "Win32Window.h"
#include <iostream>

namespace NativeDex::Platform::Windows {

bool Win32Window::classRegistered_ = false;

Win32Window::Win32Window() = default;

Win32Window::~Win32Window() {
    destroy();
}

bool Win32Window::create(const Config& config) {
    config_ = config;
    
    if (!classRegistered_) {
        registerWindowClass();
    }
    
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!config_.resizable) {
        style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    }
    
    if (config_.fullscreen) {
        style = WS_POPUP | WS_VISIBLE;
    }
    
    RECT rect = { 0, 0, config_.width, config_.height };
    AdjustWindowRect(&rect, style, FALSE);
    
    int windowWidth = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;
    
    hwnd_ = CreateWindowExW(
        0,
        CLASS_NAME,
        config_.title.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowWidth, windowHeight,
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );
    
    if (!hwnd_) {
        std::cerr << "[Win32Window] Failed to create window, error: " << GetLastError() << std::endl;
        return false;
    }
    
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    
    return true;
}

void Win32Window::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    unregisterWindowClass();
}

void Win32Window::show(int cmdShow) {
    if (hwnd_) {
        ShowWindow(hwnd_, cmdShow);
        UpdateWindow(hwnd_);
    }
}

void Win32Window::invalidate(bool eraseBackground) {
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, eraseBackground ? TRUE : FALSE);
    }
}

void Win32Window::hide() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void Win32Window::setTitle(const std::wstring& title) {
    config_.title = title;
    if (hwnd_) {
        SetWindowTextW(hwnd_, title.c_str());
    }
}

void Win32Window::setSize(int width, int height) {
    config_.width = width;
    config_.height = height;
    if (hwnd_) {
        RECT rect = { 0, 0, width, height };
        AdjustWindowRect(&rect, GetWindowLong(hwnd_, GWL_STYLE), FALSE);
        SetWindowPos(hwnd_, nullptr, 0, 0, 
            rect.right - rect.left, rect.bottom - rect.top,
            SWP_NOMOVE | SWP_NOZORDER);
    }
}

void Win32Window::getSize(int& width, int& height) const {
    if (hwnd_) {
        RECT rect;
        GetWindowRect(hwnd_, &rect);
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
    } else {
        width = config_.width;
        height = config_.height;
    }
}

void Win32Window::getClientSize(int& width, int& height) const {
    if (hwnd_) {
        RECT rect;
        GetClientRect(hwnd_, &rect);
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
    } else {
        width = config_.width;
        height = config_.height;
    }
}

int Win32Window::runMessageLoop() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void Win32Window::postQuit(int exitCode) {
    PostQuitMessage(exitCode);
}

LRESULT CALLBACK Win32Window::staticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Win32Window* window = nullptr;
    
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        window = reinterpret_cast<Win32Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        if (window) {
            window->hwnd_ = hwnd;
        }
    } else {
        window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    
    if (window) {
        return window->handleMessage(msg, wParam, lParam);
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Win32Window::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (messageCallback_) {
        LRESULT result = messageCallback_(hwnd_, msg, wParam, lParam);
        if (result != 0) return result;
    }
    
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd_, &ps);
            if (paintCallback_) {
                paintCallback_(hdc);
            }
            EndPaint(hwnd_, &ps);
            return 0;
        }
        
        case WM_SIZE: {
            if (resizeCallback_) {
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);
                resizeCallback_(width, height);
            }
            break;
        }
        
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            if (keyboardCallback_) {
                keyboardCallback_(msg, wParam, lParam);
            }
            break;
        
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEWHEEL:
            if (mouseCallback_) {
                mouseCallback_(msg, wParam, lParam);
            }
            break;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
            
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
            
        case WM_ERASEBKGND:
            return 1; // We handle background in WM_PAINT
    }
    
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void Win32Window::registerWindowClass() {
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = staticWndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = GetModuleHandle(nullptr);
    wcex.hIcon = config_.icon ? config_.icon : LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = config_.cursor;
    wcex.hbrBackground = config_.background;
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = CLASS_NAME;
    wcex.hIconSm = wcex.hIcon;
    
    if (!RegisterClassExW(&wcex)) {
        std::cerr << "[Win32Window] Failed to register window class" << std::endl;
    } else {
        classRegistered_ = true;
    }
}

void Win32Window::unregisterWindowClass() {
    if (classRegistered_) {
        UnregisterClassW(CLASS_NAME, GetModuleHandle(nullptr));
        classRegistered_ = false;
    }
}

} // namespace NativeDex::Platform::Windows