#pragma once

#include <mutex>
#include <condition_variable>
#include <memory>
#include "Message.h"

namespace NativeDex::Android::OS {

class MessageQueue {
public:
    MessageQueue();
    ~MessageQueue();
    
    // Get next message (blocks until available or quit)
    Message* next();
    
    // Enqueue message
    bool enqueueMessage(Message* msg, int64_t when = 0);
    bool enqueueMessageAtTime(Message* msg, int64_t uptimeMillis);
    
    // Remove messages
    void removeMessages(Handler* handler, int what, void* obj);
    void removeCallbacksAndMessages(Handler* handler, void* obj);
    
    // Check for messages
    bool hasMessages(Handler* handler, int what, void* obj);
    
    // Quit the queue
    void quit();
    void quitSafely();
    
    // Idle handler support
    using IdleHandler = std::function<bool()>;
    void addIdleHandler(IdleHandler handler);
    void removeIdleHandler(IdleHandler handler);

private:
    Message* messages_ = nullptr;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool quitting_ = false;
    bool quitAllowed_ = true;
    
    std::vector<IdleHandler> idleHandlers_;
};

} // namespace NativeDex::Android::OS