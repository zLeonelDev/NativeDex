#pragma once

#include <windows.h>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <future>
#include <atomic>

namespace NativeDex::Platform::Windows {

class Win32Thread {
public:
    using ThreadFunc = std::function<void()>;
    
    Win32Thread() = default;
    ~Win32Thread() { join(); }
    
    bool start(ThreadFunc func);
    void join();
    void detach();
    bool isJoinable() const { return thread_.joinable(); }
    std::thread::id getId() const { return thread_.get_id(); }
    
    static void sleep(int milliseconds) { Sleep(milliseconds); }
    static void yield() { SwitchToThread(); }
    static unsigned int getCurrentThreadId() { return GetCurrentThreadId(); }
    static HANDLE getCurrentThreadHandle() { return GetCurrentThread(); }
    
    // Thread-local storage
    static void setTlsValue(void* value) { TlsSetValue(tlsIndex_, value); }
    static void* getTlsValue() { return TlsGetValue(tlsIndex_); }
    
    // Thread priority
    enum class Priority {
        Idle = THREAD_PRIORITY_IDLE,
        Lowest = THREAD_PRIORITY_LOWEST,
        BelowNormal = THREAD_PRIORITY_BELOW_NORMAL,
        Normal = THREAD_PRIORITY_NORMAL,
        AboveNormal = THREAD_PRIORITY_ABOVE_NORMAL,
        Highest = THREAD_PRIORITY_HIGHEST,
        TimeCritical = THREAD_PRIORITY_TIME_CRITICAL
    };
    
    bool setPriority(Priority priority);
    Priority getPriority();

private:
    std::thread thread_;
    static DWORD tlsIndex_;
    
    static void initializeTls() {
        if (tlsIndex_ == TLS_OUT_OF_INDEXES) {
            tlsIndex_ = TlsAlloc();
        }
    }
};

class Win32Mutex {
public:
    Win32Mutex() { InitializeCriticalSection(&cs_); }
    ~Win32Mutex() { DeleteCriticalSection(&cs_); }
    
    void lock() { EnterCriticalSection(&cs_); }
    void unlock() { LeaveCriticalSection(&cs_); }
    bool tryLock() { return TryEnterCriticalSection(&cs_) != FALSE; }
    
    CRITICAL_SECTION* nativeHandle() { return &cs_; }
    
private:
    CRITICAL_SECTION cs_;
};

class Win32ConditionVariable {
public:
    Win32ConditionVariable() { InitializeConditionVariable(&cv_); }
    ~Win32ConditionVariable() = default;
    
    void notifyOne() { WakeConditionVariable(&cv_); }
    void notifyAll() { WakeAllConditionVariable(&cv_); }
    
    template<typename Mutex>
    void wait(Mutex& mutex) {
        SleepConditionVariableCS(&cv_, mutex.nativeHandle(), INFINITE);
    }
    
    template<typename Mutex, typename Predicate>
    void wait(Mutex& mutex, Predicate pred) {
        while (!pred()) {
            wait(mutex);
        }
    }
    
    template<typename Mutex, typename Rep, typename Period>
    bool waitFor(Mutex& mutex, const std::chrono::duration<Rep, Period>& timeout) {
        DWORD ms = static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
        return SleepConditionVariableCS(&cv_, mutex.nativeHandle(), ms) != FALSE;
    }
    
private:
    CONDITION_VARIABLE cv_;
};

class Win32ThreadPool {
public:
    Win32ThreadPool(int threadCount = 0);
    ~Win32ThreadPool();
    
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using ReturnType = decltype(f(args...));
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<ReturnType> result = task->get_future();
        
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            tasks_.emplace([task]() { (*task)(); });
        }
        condition_.notify_one();
        return result;
    }
    
    void shutdown();
    size_t getQueueSize() const;
    int getThreadCount() const;
    
private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex queueMutex_;
    std::condition_variable condition_;
    bool stop_ = false;
};

class Win32Event {
public:
    Win32Event(bool manualReset = false, bool initialState = false) {
        handle_ = CreateEvent(nullptr, manualReset, initialState, nullptr);
    }
    
    ~Win32Event() {
        if (handle_) CloseHandle(handle_);
    }
    
    void set() { SetEvent(handle_); }
    void reset() { ResetEvent(handle_); }
    bool wait(DWORD timeout = INFINITE) {
        return WaitForSingleObject(handle_, timeout) == WAIT_OBJECT_0;
    }
    
    HANDLE getHandle() const { return handle_; }
    
private:
    HANDLE handle_ = nullptr;
};

} // namespace NativeDex::Platform::Windows