#include "Win32Time.h"
#include <ctime>
#include <iomanip>
#include <sstream>

namespace NativeDex::Platform::Windows {

std::string Win32Time::formatTime(int64_t millis, const char* format) {
    std::time_t time = static_cast<std::time_t>(millis / 1000);
    std::tm tm = *std::localtime(&time);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, format);
    
    // Add milliseconds
    int ms = static_cast<int>(millis % 1000);
    oss << '.' << std::setfill('0') << std::setw(3) << ms;
    
    return oss.str();
}

int64_t Win32Time::parseTime(const char* str, const char* format) {
    std::tm tm = {};
    std::istringstream iss(str);
    iss >> std::get_time(&tm, format);
    if (iss.fail()) return 0;
    
    std::time_t time = std::mktime(&tm);
    return static_cast<int64_t>(time) * 1000;
}

} // namespace NativeDex::Platform::Windows