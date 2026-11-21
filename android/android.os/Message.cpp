#include "Message.h"
#include "Handler.h"
#include <iostream>

namespace NativeDex::Android::OS {

std::unique_ptr<Message[]> Message::pool_;
size_t Message::poolSize_ = 50;
size_t Message::poolIndex_ = 0;
std::mutex Message::poolMutex_;

Message* Message::obtain() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    if (!pool_) {
        pool_ = std::make_unique<Message[]>(poolSize_);
    }
    
    if (poolIndex_ < poolSize_) {
        Message* msg = &pool_[poolIndex_++];
        msg->inUse = true;
        return msg;
    }
    
    // Pool exhausted, create new
    return new Message();
}

Message* Message::obtain(Handler* handler, int what, void* obj) {
    Message* msg = obtain();
    msg->what = what;
    msg->obj = obj;
    msg->target = handler;
    return msg;
}

Message* Message::obtain(Handler* handler, int what, int arg1, int arg2, void* obj) {
    Message* msg = obtain();
    msg->what = what;
    msg->arg1 = arg1;
    msg->arg2 = arg2;
    msg->obj = obj;
    msg->target = handler;
    return msg;
}

Message* Message::obtain(Handler* handler, std::function<void()> callback) {
    Message* msg = obtain();
    msg->callback = std::move(callback);
    msg->target = handler;
    return msg;
}

void Message::recycle() {
    if (inUse) {
        clear();
        // In real impl, would return to pool
        inUse = false;
    }
}

void Message::clear() {
    what = 0;
    arg1 = 0;
    arg2 = 0;
    obj = nullptr;
    when = 0;
    target = nullptr;
    callback = nullptr;
    next = nullptr;
}

void Message::copyFrom(const Message* other) {
    what = other->what;
    arg1 = other->arg1;
    arg2 = other->arg2;
    obj = other->obj;
    when = other->when;
    target = other->target;
    callback = other->callback;
}

std::string Message::toString() const {
    return "Message{what=" + std::to_string(what) + 
           ", arg1=" + std::to_string(arg1) +
           ", arg2=" + std::to_string(arg2) +
           ", obj=" + (obj ? "0x" + std::to_string(reinterpret_cast<uintptr_t>(obj)) : "null") +
           ", target=" + (target ? "Handler" : "null") + "}";
}

} // namespace NativeDex::Android::OS