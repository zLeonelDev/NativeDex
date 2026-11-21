#pragma once

#include <string>

namespace NativeDex::Runtime::CacheManager {

class CacheManager {
public:
    CacheManager() = default;
    ~CacheManager() = default;

    // Save used modules for an application.
    bool saveCache(const std::string& appId, const std::string& jsonContent);

    // Load cache for an application.
    std::string loadCache(const std::string& appId);
};

} // namespace NativeDex::Runtime::CacheManager
