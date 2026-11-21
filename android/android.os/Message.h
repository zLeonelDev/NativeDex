#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <mutex>

namespace NativeDex::Android::OS {

class Handler;

class Message {
public:
    // Message identifiers
    int what = 0;
    int arg1 = 0;
    int arg2 = 0;
    void* obj = nullptr;
    int64_t when = 0;
    
    // Target handler
    Handler* target = nullptr;
    
    // Callback (for post)
    std::function<void()> callback;
    
    // Next message in queue
    Message* next = nullptr;
    
    // Flags
    bool inUse = false;
    
    static Message* obtain();
    static Message* obtain(Handler* handler, int what, void* obj = nullptr);
    static Message* obtain(Handler* handler, int what, int arg1, int arg2, void* obj = nullptr);
    static Message* obtain(Handler* handler, std::function<void()> callback);
    
    void recycle();
    void clear();
    void copyFrom(const Message* other);
    
    // Description for debugging
    std::string toString() const;

private:
    static std::unique_ptr<Message[]> pool_;
    static size_t poolSize_;
    static size_t poolIndex_;
    static std::mutex poolMutex_;
};

} // namespace NativeDex::Android::OS