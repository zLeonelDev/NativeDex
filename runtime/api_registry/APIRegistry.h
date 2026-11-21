#pragma once

#include <string>
#include <unordered_map>
#include <functional>

namespace NativeDex::Runtime::APIRegistry {

using ClassFactory = std::function<void*()>; // Placeholder for factory returning instance pointer.
using MethodCallback = std::function<void()>; // Placeholder for method implementation.

class APIRegistry {
public:
    static APIRegistry& instance();

    // Register a class with a factory.
    void registerClass(const std::string& className, ClassFactory factory);

    // Register a method with a callback.
    void registerMethod(const std::string& methodSignature, MethodCallback callback);

    // Lookup helpers.
    ClassFactory getClassFactory(const std::string& className) const;
    MethodCallback getMethodCallback(const std::string& methodSignature) const;

private:
    APIRegistry() = default;
    std::unordered_map<std::string, ClassFactory> classMap_;
    std::unordered_map<std::string, MethodCallback> methodMap_;
};

} // namespace NativeDex::Runtime::APIRegistry
