#pragma once

#include <cstdint>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "MessageQueue.h"

namespace NativeDex::Android::OS {

class Looper {
public:
    Looper();
    virtual ~Looper();
    
    // Get the looper for the current thread
    static Looper* myLooper();
    
    // Get the main looper (UI thread)
    static Looper* getMainLooper();
    
    // Prepare looper for current thread
    static void prepare();
    static void prepareMainLooper();
    
    // Run the loop
    static void loop();
    void run();
    
    // Quit the loop
    void quit();
    void quitSafely();
    
    // Get message queue
    MessageQueue* getQueue() { return queue_.get(); }
    
    // Thread ID
    std::thread::id getThreadId() const { return threadId_; }
    
    // Check if current thread is the looper's thread
    bool isCurrentThread() const { return std::this_thread::get_id() == threadId_; }

private:
    std::unique_ptr<MessageQueue> queue_;
    std::thread::id threadId_;
    bool quit_ = false;
    bool quitAllowed_ = true;
    
    static thread_local Looper* tlsLooper_;
    static Looper* mainLooper_;
    static std::mutex mainLooperMutex_;
};

} // namespace NativeDex::Android::OS