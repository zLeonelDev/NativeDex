#include "Win32Input.h"
#include <windowsx.h>
#include <iostream>

namespace NativeDex::Platform::Windows {

void Win32Input::initialize(HWND hwnd) {
    hwnd_ = hwnd;
    keyStates_.resize(256, false);
    
    // Initialize touch injection
    // InitializeTouchInjection(10, TOUCH_FEEDBACK_DEFAULT);
    touchInjectorInitialized_ = true;
}

void Win32Input::shutdown() {
    hwnd_ = nullptr;
}

bool Win32Input::isKeyDown(int keyCode) const {
    if (keyCode >= 0 && keyCode < 256) {
        return keyStates_[keyCode];
    }
    return false;
}

bool Win32Input::isKeyUp(int keyCode) const {
    return !isKeyDown(keyCode);
}

bool Win32Input::isKeyToggled(int keyCode) const {
    return (GetKeyState(keyCode) & 1) != 0;
}

void Win32Input::getMousePosition(int& x, int& y) const {
    x = mouseX_;
    y = mouseY_;
}

bool Win32Input::processMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            int vkCode = static_cast<int>(wParam);
            bool extended = (lParam >> 24) & 1;
            int scanCode = (lParam >> 16) & 0xFF;
            int repeatCount = lParam & 0xFFFF;
            bool altDown = (lParam >> 29) & 1;
            
            {
                std::lock_guard<std::mutex> lock(keyStatesMutex_);
                if (vkCode >= 0 && vkCode < 256) {
                    keyStates_[vkCode] = true;
                }
            }
            
            if (keyCallback_) {
                KeyEvent event;
                event.type = KeyEvent::DOWN;
                event.keyCode = vkCode;
                event.scanCode = scanCode;
                event.extended = extended;
                event.altDown = altDown;
                event.repeatCount = repeatCount;
                keyCallback_(event);
            }
            return true;
        }
        
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            int vkCode = static_cast<int>(wParam);
            int scanCode = (lParam >> 16) & 0xFF;
            bool extended = (lParam >> 24) & 1;
            
            {
                std::lock_guard<std::mutex> lock(keyStatesMutex_);
                if (vkCode >= 0 && vkCode < 256) {
                    keyStates_[vkCode] = false;
                }
            }
            
            if (keyCallback_) {
                KeyEvent event;
                event.type = KeyEvent::UP;
                event.keyCode = vkCode;
                event.scanCode = scanCode;
                event.extended = extended;
                event.repeatCount = 0;
                keyCallback_(event);
            }
            return true;
        }
        
        case WM_CHAR: {
            if (keyCallback_) {
                KeyEvent event;
                event.type = KeyEvent::CHAR;
                event.keyCode = 0;
                event.character = static_cast<wchar_t>(wParam);
                keyCallback_(event);
            }
            return true;
        }
        
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            mouseX_ = x;
            mouseY_ = y;
            
            if (!mouseTracked_) {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd_, 0 };
                TrackMouseEvent(&tme);
                mouseTracked_ = true;
            }
            
            if (mouseCallback_) {
                MouseEvent event;
                event.type = MouseEvent::MOVE;
                event.x = x;
                event.y = y;
                event.buttonState = static_cast<WPARAM>(wParam);
                mouseCallback_(event);
            }
            return true;
        }
        
        case WM_MOUSELEAVE: {
            mouseTracked_ = false;
            return true;
        }
        
        case WM_LBUTTONDOWN: {
            SetCapture(hwnd_);
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            mouseX_ = x;
            mouseY_ = y;
            
            if (mouseCallback_) {
                MouseEvent event;
                event.type = MouseEvent::LEFT_DOWN;
                event.x = x;
                event.y = y;
                event.buttonState = static_cast<WPARAM>(wParam);
                mouseCallback_(event);
            }
            return true;
        }
        
        case WM_LBUTTONUP: {
            ReleaseCapture();
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            mouseX_ = x;
            mouseY_ = y;
            
            if (mouseCallback_) {
                MouseEvent event;
                event.type = MouseEvent::LEFT_UP;
                event.x = x;
                event.y = y;
                event.buttonState = static_cast<WPARAM>(wParam);
                mouseCallback_(event);
            }
            return true;
        }
        
        case WM_RBUTTONDOWN: {
            SetCapture(hwnd_);
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            mouseX_ = x;
            mouseY_ = y;
            
            if (mouseCallback_) {
                MouseEvent event;
                event.type = MouseEvent::RIGHT_DOWN;
                event.x = x;
                event.y = y;
                event.buttonState = static_cast<WPARAM>(wParam);
                mouseCallback_(event);
            }
            return true;
        }
        
        case WM_RBUTTONUP: {
            ReleaseCapture();
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            mouseX_ = x;
            mouseY_ = y;
            
            if (mouseCallback_) {
                MouseEvent event;
                event.type = MouseEvent::RIGHT_UP;
                event.x = x;
                event.y = y;
                event.buttonState = static_cast<WPARAM>(wParam);
                mouseCallback_(event);
            }
            return true;
        }
        
        case WM_MBUTTONDOWN: {
            SetCapture(hwnd_);
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            mouseX_ = x;
            mouseY_ = y;
            
            if (mouseCallback_) {
                MouseEvent event;
                event.type = MouseEvent::MIDDLE_DOWN;
                event.x = x;
                event.y = y;
                event.buttonState = static_cast<WPARAM>(wParam);
                mouseCallback_(event);
            }
            return true;
        }
        
        case WM_MBUTTONUP: {
            ReleaseCapture();
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            mouseX_ = x;
            mouseY_ = y;
            
            if (mouseCallback_) {
                MouseEvent event;
                event.type = MouseEvent::MIDDLE_UP;
                event.x = x;
                event.y = y;
                event.buttonState = static_cast<WPARAM>(wParam);
                mouseCallback_(event);
            }
            return true;
        }
        
        case WM_MOUSEWHEEL: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            // Convert to client coordinates
            POINT pt = { x, y };
            ScreenToClient(hwnd_, &pt);
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            
            if (mouseCallback_) {
                MouseEvent event;
                event.type = MouseEvent::WHEEL;
                event.x = pt.x;
                event.y = pt.y;
                event.wheelDelta = delta;
                event.buttonState = static_cast<WPARAM>(wParam);
                mouseCallback_(event);
            }
            return true;
        }
        
        case WM_TOUCH: {
            if (touchCallback_) {
                UINT numInputs = LOWORD(wParam);
                TOUCHINPUT* inputs = new TOUCHINPUT[numInputs];
                if (GetTouchInputInfo(reinterpret_cast<HTOUCHINPUT>(lParam), numInputs, inputs, sizeof(TOUCHINPUT))) {
                    for (UINT i = 0; i < numInputs; ++i) {
                        TouchEvent event;
                        if (inputs[i].dwFlags & TOUCHEVENTF_DOWN) event.type = TouchEvent::DOWN;
                        else if (inputs[i].dwFlags & TOUCHEVENTF_UP) event.type = TouchEvent::UP;
                        else if (inputs[i].dwFlags & TOUCHEVENTF_MOVE) event.type = TouchEvent::MOVE;
                        
                        event.id = inputs[i].dwID;
                        event.x = inputs[i].x / 100; // Touch coordinates are in hundredths of pixels
                        event.y = inputs[i].y / 100;
                        event.pressure = 1.0f; // Simplified
                        
                        touchCallback_(event);
                    }
                }
                delete[] inputs;
                CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lParam));
            }
            return true;
        }
    }
    return false;
}

int Win32Input::win32KeyToAndroid(int vkCode) {
    // Map Win32 VK_* to Android KeyEvent.KEYCODE_*
    switch (vkCode) {
        case VK_BACK: return 67;      // KEYCODE_DEL
        case VK_TAB: return 61;       // KEYCODE_TAB
        case VK_RETURN: return 66;    // KEYCODE_ENTER
        case VK_SHIFT: return 59;     // KEYCODE_SHIFT_LEFT
        case VK_CONTROL: return 113;  // KEYCODE_CTRL_LEFT
        case VK_MENU: return 57;      // KEYCODE_ALT_LEFT
        case VK_PAUSE: return 121;    // KEYCODE_BREAK
        case VK_CAPITAL: return 115;  // KEYCODE_CAPS_LOCK
        case VK_ESCAPE: return 111;   // KEYCODE_ESCAPE
        case VK_SPACE: return 62;     // KEYCODE_SPACE
        case VK_PRIOR: return 92;     // KEYCODE_PAGE_UP
        case VK_NEXT: return 93;      // KEYCODE_PAGE_DOWN
        case VK_END: return 123;      // KEYCODE_MOVE_END
        case VK_HOME: return 122;     // KEYCODE_MOVE_HOME
        case VK_LEFT: return 21;      // KEYCODE_DPAD_LEFT
        case VK_UP: return 19;        // KEYCODE_DPAD_UP
        case VK_RIGHT: return 22;     // KEYCODE_DPAD_RIGHT
        case VK_DOWN: return 20;      // KEYCODE_DPAD_DOWN
        case VK_INSERT: return 124;   // KEYCODE_INSERT
        case VK_DELETE: return 67;    // KEYCODE_DEL (Forward Delete)
        case VK_LWIN: return 117;     // KEYCODE_META_LEFT
        case VK_RWIN: return 118;     // KEYCODE_META_RIGHT
        case VK_APPS: return 119;     // KEYCODE_MENU
        case VK_NUMPAD0: return 144;  // KEYCODE_NUMPAD_0
        case VK_NUMPAD1: return 145;  // KEYCODE_NUMPAD_1
        case VK_NUMPAD2: return 146;  // KEYCODE_NUMPAD_2
        case VK_NUMPAD3: return 147;  // KEYCODE_NUMPAD_3
        case VK_NUMPAD4: return 148;  // KEYCODE_NUMPAD_4
        case VK_NUMPAD5: return 149;  // KEYCODE_NUMPAD_5
        case VK_NUMPAD6: return 150;  // KEYCODE_NUMPAD_6
        case VK_NUMPAD7: return 151;  // KEYCODE_NUMPAD_7
        case VK_NUMPAD8: return 152;  // KEYCODE_NUMPAD_8
        case VK_NUMPAD9: return 153;  // KEYCODE_NUMPAD_9
        case VK_MULTIPLY: return 155; // KEYCODE_NUMPAD_MULTIPLY
        case VK_ADD: return 157;      // KEYCODE_NUMPAD_ADD
        case VK_SUBTRACT: return 156; // KEYCODE_NUMPAD_SUBTRACT
        case VK_DECIMAL: return 158;  // KEYCODE_NUMPAD_DOT
        case VK_DIVIDE: return 154;   // KEYCODE_NUMPAD_DIVIDE
        case VK_F1: return 131;       // KEYCODE_F1
        case VK_F2: return 132;       // KEYCODE_F2
        case VK_F3: return 133;       // KEYCODE_F3
        case VK_F4: return 134;       // KEYCODE_F4
        case VK_F5: return 135;       // KEYCODE_F5
        case VK_F6: return 136;       // KEYCODE_F6
        case VK_F7: return 137;       // KEYCODE_F7
        case VK_F8: return 138;       // KEYCODE_F8
        case VK_F9: return 139;       // KEYCODE_F9
        case VK_F10: return 140;      // KEYCODE_F10
        case VK_F11: return 141;      // KEYCODE_F11
        case VK_F12: return 142;      // KEYCODE_F12
        case VK_NUMLOCK: return 143;  // KEYCODE_NUM_LOCK
        case VK_SCROLL: return 116;   // KEYCODE_SCROLL_LOCK
        default:
            if (vkCode >= '0' && vkCode <= '9') return 7 + (vkCode - '0');     // KEYCODE_0-9
            if (vkCode >= 'A' && vkCode <= 'Z') return 29 + (vkCode - 'A');     // KEYCODE_A-Z
            return 0; // KEYCODE_UNKNOWN
    }
}

int Win32Input::win32MouseToAndroidButton(int mkFlags) {
    int buttons = 0;
    if (mkFlags & MK_LBUTTON) buttons |= AndroidMotionEvent::BUTTON_PRIMARY;
    if (mkFlags & MK_RBUTTON) buttons |= AndroidMotionEvent::BUTTON_SECONDARY;
    if (mkFlags & MK_MBUTTON) buttons |= AndroidMotionEvent::BUTTON_TERTIARY;
    if (mkFlags & MK_XBUTTON1) buttons |= AndroidMotionEvent::BUTTON_BACK;
    if (mkFlags & MK_XBUTTON2) buttons |= AndroidMotionEvent::BUTTON_FORWARD;
    return buttons;
}

Win32Input::AndroidKeyEvent Win32Input::convertKeyEvent(const KeyEvent& event) {
    AndroidKeyEvent androidEvent;
    androidEvent.action = (event.type == KeyEvent::DOWN) ? AndroidKeyEvent::DOWN : 
                          (event.type == KeyEvent::UP) ? AndroidKeyEvent::UP : AndroidKeyEvent::MULTIPLE;
    androidEvent.keyCode = win32KeyToAndroid(event.keyCode);
    androidEvent.scanCode = event.scanCode;
    androidEvent.metaState = 0; // Would track modifier keys
    androidEvent.flags = event.extended ? 1 : 0;
    androidEvent.repeatCount = event.repeatCount;
    androidEvent.character = event.character;
    androidEvent.eventTime = GetTickCount64();
    return androidEvent;
}

Win32Input::AndroidMotionEvent Win32Input::convertMouseEvent(const MouseEvent& event) {
    AndroidMotionEvent androidEvent;
    androidEvent.action = AndroidMotionEvent::MOVE;
    androidEvent.actionIndex = -1;
    androidEvent.pointerCount = 1;
    androidEvent.pointerIds[0] = 0;
    androidEvent.pointerX[0] = static_cast<float>(event.x);
    androidEvent.pointerY[0] = static_cast<float>(event.y);
    androidEvent.pointerPressure[0] = 1.0f;
    androidEvent.pointerSize[0] = 1.0f;
    androidEvent.metaState = 0;
    androidEvent.buttonState = win32MouseToAndroidButton(event.buttonState);
    androidEvent.toolType = AndroidMotionEvent::MOUSE;
    androidEvent.edgeFlags = 0;
    androidEvent.eventTime = GetTickCount64();
    androidEvent.downTime = androidEvent.eventTime;
    
    switch (event.type) {
        case MouseEvent::LEFT_DOWN:
            androidEvent.action = AndroidMotionEvent::DOWN;
            break;
        case MouseEvent::LEFT_UP:
            androidEvent.action = AndroidMotionEvent::UP;
            break;
        case MouseEvent::RIGHT_DOWN:
            androidEvent.action = AndroidMotionEvent::DOWN;
            break;
        case MouseEvent::RIGHT_UP:
            androidEvent.action = AndroidMotionEvent::UP;
            break;
        case MouseEvent::MIDDLE_DOWN:
            androidEvent.action = AndroidMotionEvent::DOWN;
            break;
        case MouseEvent::MIDDLE_UP:
            androidEvent.action = AndroidMotionEvent::UP;
            break;
        default:
            break;
    }
    
    return androidEvent;
}

Win32Input::AndroidMotionEvent Win32Input::convertTouchEvent(const TouchEvent& event) {
    AndroidMotionEvent androidEvent;
    androidEvent.action = AndroidMotionEvent::MOVE;
    androidEvent.actionIndex = -1;
    androidEvent.pointerCount = 1;
    androidEvent.pointerIds[0] = event.id;
    androidEvent.pointerX[0] = static_cast<float>(event.x);
    androidEvent.pointerY[0] = static_cast<float>(event.y);
    androidEvent.pointerPressure[0] = event.pressure;
    androidEvent.pointerSize[0] = 1.0f;
    androidEvent.metaState = 0;
    androidEvent.buttonState = 0;
    androidEvent.toolType = AndroidMotionEvent::FINGER;
    androidEvent.edgeFlags = 0;
    androidEvent.eventTime = GetTickCount64();
    androidEvent.downTime = androidEvent.eventTime;
    
    switch (event.type) {
        case TouchEvent::DOWN:
            androidEvent.action = AndroidMotionEvent::DOWN;
            break;
        case TouchEvent::UP:
            androidEvent.action = AndroidMotionEvent::UP;
            break;
        case TouchEvent::MOVE:
            androidEvent.action = AndroidMotionEvent::MOVE;
            break;
        case TouchEvent::CANCEL:
            androidEvent.action = AndroidMotionEvent::CANCEL;
            break;
    }
    
    return androidEvent;
}

} // namespace NativeDex::Platform::Windows