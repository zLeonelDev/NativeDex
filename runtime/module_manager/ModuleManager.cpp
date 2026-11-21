#include "ModuleManager.h"
#include <iostream>

namespace NativeDex::Runtime::ModuleManager {

bool ModuleManager::loadModule(const std::string& moduleName) {
    // Placeholder: Dynamically load module (e.g., shared library) when needed.
    std::cout << "[ModuleManager] Loading module: " << moduleName << std::endl;
    return true;
}

} // namespace NativeDex::Runtime::ModuleManager
