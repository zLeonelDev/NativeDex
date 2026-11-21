#pragma once

#include <string>

namespace NativeDex::Runtime::MethodResolver {

class MethodResolver {
public:
    MethodResolver() = default;
    ~MethodResolver() = default;

    // Resolve a method call based on class and method name.
    bool resolve(const std::string& className, const std::string& methodName);
};

} // namespace NativeDex::Runtime::MethodResolver
