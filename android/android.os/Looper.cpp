#include "Looper.h"
#include "MessageQueue.h"
#include "Handler.h"
#include <iostream>

namespace NativeDex::Android::OS {

thread_local Looper* Looper::tlsLooper_ = nullptr;
Looper* Looper::mainLooper_ = nullptr;
std::mutex Looper::mainLooperMutex_;

Looper::Looper() : threadId_(std::this_thread::get_id()) {
    queue_ = std::make_unique<MessageQueue>();
}

Looper::~Looper() {
    if (tlsLooper_ == this) {
        tlsLooper_ = nullptr;
    }
}

Looper* Looper::myLooper() {
    return tlsLooper_;
}

Looper* Looper::getMainLooper() {
    std::lock_guard<std::mutex> lock(mainLooperMutex_);
    return mainLooper_;
}

void Looper::prepare() {
    if (tlsLooper_) {
        throw std::runtime_error("Only one Looper may be created per thread");
    }
    tlsLooper_ = new Looper();
}

void Looper::prepareMainLooper() {
    prepare();
    std::lock_guard<std::mutex> lock(mainLooperMutex_);
    mainLooper_ = tlsLooper_;
}

void Looper::loop() {
    if (tlsLooper_) {
        tlsLooper_->run();
    }
}

void Looper::run() {
    while (!quit_) {
        Message* msg = queue_->next();
        if (!msg) break;
        
        msg->target->dispatchMessage(msg);
        
        // Recycle message
        msg->recycle();
    }
}

void Looper::quit() {
    quit_ = true;
    queue_->quit();
}

void Looper::quitSafely() {
    quitAllowed_ = true;
    quit();
}

} // namespace NativeDex::Android::OS