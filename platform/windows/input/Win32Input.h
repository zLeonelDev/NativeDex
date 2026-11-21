#pragma once

#include <windows.h>
#include <functional>
#include <vector>
#include <mutex>

namespace NativeDex::Platform::Windows {

class Win32Input {
public:
    struct KeyEvent {
        enum Type { DOWN, UP, CHAR };
        Type type;
        int keyCode;      // VK_* code
        int scanCode;     // Hardware scan code
        bool extended;    // Extended key (E0 prefix)
        bool altDown;     // Alt key state
        int repeatCount;  // Auto-repeat count
        wchar_t character; // For CHAR messages
    };
    
    struct MouseEvent {
        enum Type { MOVE, LEFT_DOWN, LEFT_UP, RIGHT_DOWN, RIGHT_UP, MIDDLE_DOWN, MIDDLE_UP, WHEEL, XBUTTON_DOWN, XBUTTON_UP };
        Type type;
        int x, y;         // Client coordinates
        int wheelDelta;   // For WHEEL events
        WPARAM buttonState; // MK_* flags
    };
    
    struct TouchEvent {
        enum Type { DOWN, UP, MOVE, CANCEL };
        Type type;
        int id;           // Touch point ID
        int x, y;         // Client coordinates
        float pressure;   // 0.0 - 1.0
    };

    using KeyCallback = std::function<void(const KeyEvent&)>;
    using MouseCallback = std::function<void(const MouseEvent&)>;
    using TouchCallback = std::function<void(const TouchEvent&)>;

    Win32Input() = default;
    ~Win32Input() = default;
    
    void initialize(HWND hwnd);
    void shutdown();
    
    void setKeyCallback(KeyCallback cb) { keyCallback_ = std::move(cb); }
    void setMouseCallback(MouseCallback cb) { mouseCallback_ = std::move(cb); }
    void setTouchCallback(TouchCallback cb) { touchCallback_ = std::move(cb); }
    
    // Query key state
    bool isKeyDown(int keyCode) const;
    bool isKeyUp(int keyCode) const;
    bool isKeyToggled(int keyCode) const; // For CapsLock, NumLock, etc.
    
    // Get mouse position
    void getMousePosition(int& x, int& y) const;
    
    // Process a single message (call from message loop)
    bool processMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    
    // Android key event conversion
    struct AndroidKeyEvent {
        enum Action { DOWN = 0, UP = 1, MULTIPLE = 2 };
        Action action;
        int keyCode;      // Android key code
        int scanCode;
        int metaState;
        int flags;
        int repeatCount;
        wchar_t character;
        long long eventTime;
    };
    
    struct AndroidMotionEvent {
        enum Action { DOWN = 0, UP = 1, MOVE = 2, CANCEL = 3, OUTSIDE = 4, POINTER_DOWN = 5, POINTER_UP = 6 };
        enum ToolType { UNKNOWN = 0, FINGER = 1, STYLUS = 2, MOUSE = 3, ERASER = 4 };
        enum ButtonState { BUTTON_PRIMARY = 1, BUTTON_SECONDARY = 2, BUTTON_TERTIARY = 4, BUTTON_BACK = 8, BUTTON_FORWARD = 16 };
        
        Action action;
        int actionIndex;  // For pointer down/up
        int pointerCount;
        int pointerIds[10];
        float pointerX[10];
        float pointerY[10];
        float pointerPressure[10];
        float pointerSize[10];
        int metaState;
        int buttonState;
        int toolType;
        int edgeFlags;
        long long eventTime;
        long long downTime;
    };
    
    AndroidKeyEvent convertKeyEvent(const KeyEvent& event);
    AndroidMotionEvent convertMouseEvent(const MouseEvent& event);
    AndroidMotionEvent convertTouchEvent(const TouchEvent& event);

private:
    HWND hwnd_ = nullptr;
    KeyCallback keyCallback_;
    MouseCallback mouseCallback_;
    TouchCallback touchCallback_;
    
    std::vector<bool> keyStates_;
    std::mutex keyStatesMutex_;
    
    int mouseX_ = 0;
    int mouseY_ = 0;
    bool mouseTracked_ = false;
    
    // Touch injection
    bool touchInjectorInitialized_ = false;
    
    static int win32KeyToAndroid(int vkCode);
    static int win32MouseToAndroidButton(int mkFlags);
};

} // namespace NativeDex::Platform::Windows