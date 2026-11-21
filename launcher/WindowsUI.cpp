#include "WindowsUI.h"
#include <windows.h>
#include <string>
#include <mutex>

namespace NativeDex::Launcher {

static HWND g_mainWindow = nullptr;
static std::string g_windowTitle;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            FillRect(hdc, &ps.rcPaint, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

bool createNativeWindow(const std::string& title, const XmlNode* layoutRoot) {
    g_windowTitle = title.empty() ? "NativeDex App" : title;

    HINSTANCE hInstance = GetModuleHandle(nullptr);
    const wchar_t className[] = L"NativeDexMainWindow";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) {
        return false;
    }

    g_mainWindow = CreateWindowExW(
        0,
        className,
        std::wstring(g_windowTitle.begin(), g_windowTitle.end()).c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1024,
        768,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!g_mainWindow) {
        return false;
    }

    ShowWindow(g_mainWindow, SW_SHOW);
    UpdateWindow(g_mainWindow);
    return true;
}

int runNativeMessageLoop() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

} // namespace NativeDex::Launcher
