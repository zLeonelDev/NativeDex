#include "Win32Thread.h"
#include <windows.h>

namespace NativeDex::Platform::Windows {

DWORD Win32Thread::tlsIndex_ = TLS_OUT_OF_INDEXES;

bool Win32Thread::start(ThreadFunc func) {
    if (thread_.joinable()) return false;
    initializeTls();
    thread_ = std::thread([this, func = std::move(func)]() {
        setTlsValue(this);
        func();
    });
    return true;
}

void Win32Thread::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Win32Thread::detach() {
    if (thread_.joinable()) {
        thread_.detach();
    }
}

bool Win32Thread::setPriority(Priority priority) {
    if (!thread_.joinable()) return false;
    return SetThreadPriority(thread_.native_handle(), static_cast<int>(priority)) != FALSE;
}

Win32Thread::Priority Win32Thread::getPriority() {
    if (!thread_.joinable()) return Priority::Normal;
    return static_cast<Priority>(GetThreadPriority(thread_.native_handle()));
}

Win32ThreadPool::Win32ThreadPool(int threadCount) {
    if (threadCount <= 0) {
        threadCount = std::thread::hardware_concurrency();
        if (threadCount == 0) threadCount = 4;
    }
    
    for (int i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queueMutex_);
                    condition_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
                    if (stop_ && tasks_.empty()) return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            }
        });
    }
}

Win32ThreadPool::~Win32ThreadPool() {
    shutdown();
}

void Win32ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stop_ = true;
    }
    condition_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

size_t Win32ThreadPool::getQueueSize() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return tasks_.size();
}

int Win32ThreadPool::getThreadCount() const {
    return static_cast<int>(workers_.size());
}

} // namespace NativeDex::Platform::Windows