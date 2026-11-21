#include "APIRegistry.h"
#include <iostream>

namespace NativeDex::Runtime::APIRegistry {

APIRegistry& APIRegistry::instance() {
    static APIRegistry instance;
    return instance;
}

void APIRegistry::registerClass(const std::string& className, ClassFactory factory) {
    classMap_[className] = std::move(factory);
    std::cout << "[APIRegistry] Registered class: " << className << std::endl;
}

void APIRegistry::registerMethod(const std::string& methodSignature, MethodCallback callback) {
    methodMap_[methodSignature] = std::move(callback);
    std::cout << "[APIRegistry] Registered method: " << methodSignature << std::endl;
}

ClassFactory APIRegistry::getClassFactory(const std::string& className) const {
    auto it = classMap_.find(className);
    if (it != classMap_.end()) {
        return it->second;
    }
    return nullptr;
}

MethodCallback APIRegistry::getMethodCallback(const std::string& methodSignature) const {
    auto it = methodMap_.find(methodSignature);
    if (it != methodMap_.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace NativeDex::Runtime::APIRegistry
