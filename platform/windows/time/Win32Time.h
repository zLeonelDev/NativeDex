#pragma once

#include <windows.h>
#include <chrono>
#include <cstdint>

namespace NativeDex::Platform::Windows {

class Win32Time {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using Duration = std::chrono::steady_clock::duration;
    
    // High-resolution timer
    static TimePoint now() { return std::chrono::steady_clock::now(); }
    
    // Current time in milliseconds since epoch (like System.currentTimeMillis)
    static int64_t currentTimeMillis() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
    
    // Current time in nanoseconds since epoch (like System.nanoTime)
    static int64_t nanoTime() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
    
    // Uptime in milliseconds (time since boot)
    static int64_t uptimeMillis() {
        return GetTickCount64();
    }
    
    // Sleep for milliseconds
    static void sleep(int64_t millis) {
        Sleep(static_cast<DWORD>(millis));
    }
    
    // Sleep for nanoseconds
    static void sleepNanos(int64_t nanos) {
        // Windows doesn't have nanosleep, use Sleep with minimum resolution
        Sleep(static_cast<DWORD>((nanos + 999999) / 1000000));
    }
    
    // High-resolution performance counter
    static int64_t performanceCounter() {
        LARGE_INTEGER count;
        QueryPerformanceCounter(&count);
        return count.QuadPart;
    }
    
    static int64_t performanceFrequency() {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return freq.QuadPart;
    }
    
    // Convert performance counter to milliseconds
    static double performanceCounterToMillis(int64_t counter) {
        return static_cast<double>(counter) * 1000.0 / static_cast<double>(performanceFrequency());
    }
    
    // Time zone offset in milliseconds
    static int32_t getTimeZoneOffset() {
        TIME_ZONE_INFORMATION tzi;
        DWORD result = GetTimeZoneInformation(&tzi);
        if (result == TIME_ZONE_ID_UNKNOWN) return 0;
        
        int32_t bias = tzi.Bias;
        if (result == TIME_ZONE_ID_DAYLIGHT) {
            bias += tzi.DaylightBias;
        }
        return -bias * 60 * 1000; // Convert minutes to milliseconds
    }
    
    // Format time as string
    static std::string formatTime(int64_t millis, const char* format = "%Y-%m-%d %H:%M:%S");
    
    // Parse time string
    static int64_t parseTime(const char* str, const char* format = "%Y-%m-%d %H:%M:%S");
    
    // Timer for measuring elapsed time
    class Timer {
    public:
        Timer() { reset(); }
        void reset() { start_ = now(); }
        double elapsedMillis() const { return std::chrono::duration<double, std::milli>(now() - start_).count(); }
        int64_t elapsedNanos() const { return std::chrono::duration_cast<std::chrono::nanoseconds>(now() - start_).count(); }
        
    private:
        TimePoint start_;
    };
};

} // namespace NativeDex::Platform::Windows