#include "Logger.h"
#include <iostream>

namespace NativeDex::Runtime::Logger {

void Logger::log(LogLevel level, const std::string& message) {
    const char* levelStr = "";
    switch (level) {
        case LogLevel::Debug:   levelStr = "[DEBUG]"; break;
        case LogLevel::Info:    levelStr = "[INFO]"; break;
        case LogLevel::Warning: levelStr = "[WARN]"; break;
        case LogLevel::Error:   levelStr = "[ERROR]"; break;
    }
    std::cout << levelStr << " " << message << std::endl;
}

} // namespace NativeDex::Runtime::Logger
