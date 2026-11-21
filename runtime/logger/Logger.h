#pragma once

#include <string>

namespace NativeDex::Runtime::Logger {

enum class LogLevel { Debug, Info, Warning, Error };

class Logger {
public:
    static void log(LogLevel level, const std::string& message);
};

} // namespace NativeDex::Runtime::Logger
