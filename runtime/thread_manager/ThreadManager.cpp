#include "ThreadManager.h"
#include <iostream>

namespace NativeDex::Runtime::ThreadManager {

void ThreadManager::runAsync(std::function<void()> func) {
    std::thread t([func = std::move(func)]() {
        func();
    });
    std::cout << "[ThreadManager] Started async thread" << std::endl;
    t.detach();
}

} // namespace NativeDex::Runtime::ThreadManager
