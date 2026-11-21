#include "System.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include "../../platform/windows/time/Win32Time.h"

namespace NativeDex::Java::Lang {

std::ostream* System::out = &std::cout;
std::ostream* System::err = &std::cerr;
std::istream* System::in = &std::cin;

std::map<std::string, std::string> System::systemProperties_;

int64_t System::currentTimeMillis() {
    return NativeDex::Platform::Windows::Win32Time::currentTimeMillis();
}

int64_t System::nanoTime() {
    return NativeDex::Platform::Windows::Win32Time::nanoTime();
}

void System::arraycopy(void* src, int srcPos, void* dest, int destPos, int length) {
    if (!src || !dest || length < 0) return;
    if (srcPos < 0 || destPos < 0) return;
    
    uint8_t* s = static_cast<uint8_t*>(src) + srcPos;
    uint8_t* d = static_cast<uint8_t*>(dest) + destPos;
    
    // Check for overlap
    if (s < d && s + length > d) {
        // Overlap - copy backwards
        s += length;
        d += length;
        while (length--) {
            *--d = *--s;
        }
    } else {
        // No overlap or safe direction - copy forwards
        std::memcpy(d, s, length);
    }
}

std::string System::getProperty(const std::string& key, const std::string& def) {
    auto it = systemProperties_.find(key);
    if (it != systemProperties_.end()) return it->second;
    return def;
}

std::string System::getProperty(const std::string& key) {
    return getProperty(key, "");
}

void System::setProperty(const std::string& key, const std::string& value) {
    systemProperties_[key] = value;
}

void System::clearProperty(const std::string& key) {
    systemProperties_.erase(key);
}

std::map<std::string, std::string> System::getProperties() {
    return systemProperties_;
}

std::string System::getenv(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : "";
}

void System::gc() {
    // Would trigger GC in runtime
}

void System::runFinalization() {
    // Would run finalizers
}

void System::exit(int status) {
    std::exit(status);
}

int System::identityHashCode(void* obj) {
    if (!obj) return 0;
    // Use pointer address as hash
    return static_cast<int>(reinterpret_cast<uintptr_t>(obj) ^ 
                           (reinterpret_cast<uintptr_t>(obj) >> 32));
}

void System::loadLibrary(const std::string& libName) {
    // Would load native library
}

void System::load(const std::string& filename) {
    // Would load native library from full path
}

void System::setIn(std::istream* in) {
    System::in = in ? in : &std::cin;
}

void System::setOut(std::ostream* out) {
    System::out = out ? out : &std::cout;
}

void System::setErr(std::ostream* err) {
    System::err = err ? err : &std::cerr;
}

void System::setSecurityManager(void* manager) {
    // Not implemented
}

void* System::getSecurityManager() {
    return nullptr;
}

// Initialize system properties
void initializeSystemProperties() {
    System::setProperty("java.version", "17");
    System::setProperty("java.vendor", "NativeDex");
    System::setProperty("java.vendor.url", "https://github.com/nativedex");
    System::setProperty("java.home", "C:/NativeDex");
    System::setProperty("java.class.version", "61.0");
    System::setProperty("os.name", "Windows 10");
    System::setProperty("os.arch", "amd64");
    System::setProperty("os.version", "10.0");
    System::setProperty("file.separator", "\\");
    System::setProperty("path.separator", ";");
    System::setProperty("line.separator", "\r\n");
    System::setProperty("user.name", "user");
    System::setProperty("user.home", "C:/Users/user");
    System::setProperty("user.dir", "C:/NativeDex");
}

} // namespace NativeDex::Java::Lang