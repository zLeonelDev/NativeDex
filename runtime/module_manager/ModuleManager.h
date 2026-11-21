#pragma once

#include <string>

namespace NativeDex::Runtime::ModuleManager {

class ModuleManager {
public:
    ModuleManager() = default;
    ~ModuleManager() = default;

    // Load a module on first use.
    bool loadModule(const std::string& moduleName);
};

} // namespace NativeDex::Runtime::ModuleManager
