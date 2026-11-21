#include "CacheManager.h"
#include <fstream>
#include <iostream>

namespace NativeDex::Runtime::CacheManager {

bool CacheManager::saveCache(const std::string& appId, const std::string& jsonContent) {
    std::ofstream out(appId + ".json");
    if (!out) {
        std::cerr << "[CacheManager] Failed to write cache for " << appId << std::endl;
        return false;
    }
    out << jsonContent;
    std::cout << "[CacheManager] Saved cache for " << appId << std::endl;
    return true;
}

std::string CacheManager::loadCache(const std::string& appId) {
    std::ifstream in(appId + ".json");
    if (!in) {
        std::cerr << "[CacheManager] No cache found for " << appId << std::endl;
        return {};
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::cout << "[CacheManager] Loaded cache for " << appId << std::endl;
    return content;
}

} // namespace NativeDex::Runtime::CacheManager
