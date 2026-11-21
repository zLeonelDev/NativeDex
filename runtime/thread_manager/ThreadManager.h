#pragma once

#include <thread>
#include <functional>

namespace NativeDex::Runtime::ThreadManager {

class ThreadManager {
public:
    ThreadManager() = default;
    ~ThreadManager() = default;

    // Run a function in a new thread.
    void runAsync(std::function<void()> func);
};

} // namespace NativeDex::Runtime::ThreadManager
