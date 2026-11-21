#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <chrono>
#include "../../platform/windows/threading/Win32Thread.h"

namespace NativeDex::Android::OS {

class Message;
class Looper;

class Handler {
public:
    using Callback = std::function<void(Message*)>;
    
    Handler();
    explicit Handler(Looper* looper);
    Handler(Looper* looper, Callback callback);
    virtual ~Handler();
    
    // Post a message to be processed later
    bool sendMessage(Message* msg);
    bool sendMessageDelayed(Message* msg, int64_t delayMillis);
    bool sendMessageAtTime(Message* msg, int64_t uptimeMillis);
    bool sendEmptyMessage(int what);
    bool sendEmptyMessageDelayed(int what, int64_t delayMillis);
    
    // Post a runnable
    bool post(std::function<void()> runnable);
    bool postDelayed(std::function<void()> runnable, int64_t delayMillis);
    bool postAtTime(std::function<void()> runnable, int64_t uptimeMillis);
    
    // Remove messages
    void removeMessages(int what);
    void removeMessages(int what, void* obj);
    void removeCallbacksAndMessages(void* obj);
    void removeCallbacks(std::function<void()> runnable);
    
    // Check for messages
    bool hasMessages(int what);
    bool hasMessages(int what, void* obj);
    
    // Get associated looper
    Looper* getLooper() const { return looper_; }
    
    // Called when message is received
    virtual void handleMessage(Message* msg);
    
    // Dispatch message (can be overridden)
    virtual void dispatchMessage(Message* msg);

protected:
    Looper* looper_ = nullptr;
    Callback callback_ = nullptr;
};

} // namespace NativeDex::Android::OS