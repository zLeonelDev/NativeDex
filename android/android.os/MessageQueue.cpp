#include "MessageQueue.h"
#include "Handler.h"
#include <chrono>
#include <algorithm>

namespace NativeDex::Android::OS {

MessageQueue::MessageQueue() = default;

MessageQueue::~MessageQueue() {
    // Clean up messages
    while (messages_) {
        Message* msg = messages_;
        messages_ = messages_->next;
        delete msg;
    }
}

Message* MessageQueue::next() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    while (!quitting_) {
        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        Message* prev = nullptr;
        Message* msg = messages_;
        
        while (msg) {
            if (msg->when <= now) {
                // Found a message to deliver
                if (prev) {
                    prev->next = msg->next;
                } else {
                    messages_ = msg->next;
                }
                msg->next = nullptr;
                return msg;
            }
            prev = msg;
            msg = msg->next;
        }
        
        // No message ready, wait
        if (messages_) {
            int64_t nextTime = messages_->when;
            int64_t waitTime = nextTime - now;
            if (waitTime > 0) {
                cv_.wait_for(lock, std::chrono::milliseconds(waitTime));
            }
        } else {
            cv_.wait(lock);
        }
    }
    
    return nullptr;
}

bool MessageQueue::enqueueMessage(Message* msg, int64_t when) {
    if (!msg) return false;
    
    if (when == 0) {
        when = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    msg->when = when;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (quitting_) {
        return false;
    }
    
    // Insert in order
    Message* prev = nullptr;
    Message* curr = messages_;
    
    while (curr && curr->when <= when) {
        prev = curr;
        curr = curr->next;
    }
    
    msg->next = curr;
    if (prev) {
        prev->next = msg;
    } else {
        messages_ = msg;
    }
    
    cv_.notify_one();
    return true;
}

bool MessageQueue::enqueueMessageAtTime(Message* msg, int64_t uptimeMillis) {
    return enqueueMessage(msg, uptimeMillis);
}

void MessageQueue::removeMessages(Handler* handler, int what, void* obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Message* prev = nullptr;
    Message* msg = messages_;
    
    while (msg) {
        if (msg->target == handler && (what == 0 || msg->what == what) && (obj == nullptr || msg->obj == obj)) {
            Message* toRemove = msg;
            msg = msg->next;
            if (prev) {
                prev->next = msg;
            } else {
                messages_ = msg;
            }
            toRemove->recycle();
        } else {
            prev = msg;
            msg = msg->next;
        }
    }
}

void MessageQueue::removeCallbacksAndMessages(Handler* handler, void* obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Message* prev = nullptr;
    Message* msg = messages_;
    
    while (msg) {
        if (msg->target == handler && (obj == nullptr || msg->obj == obj)) {
            Message* toRemove = msg;
            msg = msg->next;
            if (prev) {
                prev->next = msg;
            } else {
                messages_ = msg;
            }
            toRemove->recycle();
        } else {
            prev = msg;
            msg = msg->next;
        }
    }
}

bool MessageQueue::hasMessages(Handler* handler, int what, void* obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (Message* msg = messages_; msg; msg = msg->next) {
        if (msg->target == handler && (what == 0 || msg->what == what) && (obj == nullptr || msg->obj == obj)) {
            return true;
        }
    }
    return false;
}

void MessageQueue::quit() {
    std::lock_guard<std::mutex> lock(mutex_);
    quitting_ = true;
    cv_.notify_all();
}

void MessageQueue::quitSafely() {
    std::lock_guard<std::mutex> lock(mutex_);
    quitAllowed_ = true;
    quitting_ = true;
    cv_.notify_all();
}

void MessageQueue::addIdleHandler(IdleHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    idleHandlers_.push_back(handler);
}

void MessageQueue::removeIdleHandler(IdleHandler handler) {
    // Can't easily remove std::function, would need wrapper
}

} // namespace NativeDex::Android::OS