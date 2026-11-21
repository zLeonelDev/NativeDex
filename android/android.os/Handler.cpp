#include "Handler.h"
#include "Looper.h"
#include "Message.h"
#include <iostream>

namespace NativeDex::Android::OS {

Handler::Handler() : looper_(Looper::myLooper()) {}

Handler::Handler(Looper* looper) : looper_(looper ? looper : Looper::myLooper()) {}

Handler::Handler(Looper* looper, Callback callback) : looper_(looper ? looper : Looper::myLooper()), callback_(callback) {}

Handler::~Handler() {
    removeCallbacksAndMessages(nullptr);
}

bool Handler::sendMessage(Message* msg) {
    if (!looper_) return false;
    msg->target = this;
    return looper_->getQueue()->enqueueMessage(msg, 0);
}

bool Handler::sendMessageDelayed(Message* msg, int64_t delayMillis) {
    if (!looper_) return false;
    msg->target = this;
    return looper_->getQueue()->enqueueMessage(msg, delayMillis);
}

bool Handler::sendMessageAtTime(Message* msg, int64_t uptimeMillis) {
    if (!looper_) return false;
    msg->target = this;
    return looper_->getQueue()->enqueueMessageAtTime(msg, uptimeMillis);
}

bool Handler::sendEmptyMessage(int what) {
    return sendMessageDelayed(Message::obtain(this, what), 0);
}

bool Handler::sendEmptyMessageDelayed(int what, int64_t delayMillis) {
    return sendMessageDelayed(Message::obtain(this, what), delayMillis);
}

void Handler::removeMessages(int what) {
    if (looper_) {
        looper_->getQueue()->removeMessages(this, what, nullptr);
    }
}

void Handler::removeMessages(int what, void* obj) {
    if (looper_) {
        looper_->getQueue()->removeMessages(this, what, obj);
    }
}

void Handler::removeCallbacksAndMessages(void* obj) {
    if (looper_) {
        looper_->getQueue()->removeCallbacksAndMessages(this, obj);
    }
}

void Handler::removeCallbacks(std::function<void()> runnable) {
    // Would need to track runnables to remove them
}

bool Handler::hasMessages(int what) {
    if (looper_) {
        return looper_->getQueue()->hasMessages(this, what, nullptr);
    }
    return false;
}

bool Handler::hasMessages(int what, void* obj) {
    if (looper_) {
        return looper_->getQueue()->hasMessages(this, what, obj);
    }
    return false;
}

void Handler::handleMessage(Message* msg) {
    if (callback_) {
        callback_(msg);
    }
}

void Handler::dispatchMessage(Message* msg) {
    handleMessage(msg);
}

bool Handler::post(std::function<void()> runnable) {
    return postDelayed(std::move(runnable), 0);
}

bool Handler::postDelayed(std::function<void()> runnable, int64_t delayMillis) {
    Message* msg = Message::obtain();
    msg->callback = std::move(runnable);
    msg->target = this;
    return sendMessageDelayed(msg, delayMillis);
}

bool Handler::postAtTime(std::function<void()> runnable, int64_t uptimeMillis) {
    Message* msg = Message::obtain();
    msg->callback = std::move(runnable);
    msg->target = this;
    return sendMessageAtTime(msg, uptimeMillis);
}

} // namespace NativeDex::Android::OS