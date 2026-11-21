#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>

namespace NativeDex::Java::Lang {

class System {
public:
    // Standard streams
    static std::ostream* out;
    static std::ostream* err;
    static std::istream* in;
    
    // Time
    static int64_t currentTimeMillis();
    static int64_t nanoTime();
    
    // Array copy
    static void arraycopy(void* src, int srcPos, void* dest, int destPos, int length);
    
    // Properties
    static std::string getProperty(const std::string& key, const std::string& def = "");
    static std::string getProperty(const std::string& key);
    static void setProperty(const std::string& key, const std::string& value);
    static void clearProperty(const std::string& key);
    static std::map<std::string, std::string> getProperties();
    
    // Environment
    static std::string getenv(const std::string& name);
    
    // GC
    static void gc();
    static void runFinalization();
    
    // Exit
    static void exit(int status);
    
    // Identity hash
    static int identityHashCode(void* obj);
    
    // Load library
    static void loadLibrary(const std::string& libName);
    static void load(const std::string& filename);
    
    // Console
    static void setIn(std::istream* in);
    static void setOut(std::ostream* out);
    static void setErr(std::ostream* err);
    
    // Security
    static void setSecurityManager(void* manager);
    static void* getSecurityManager();
    
private:
    static std::map<std::string, std::string> systemProperties_;
};

} // namespace NativeDex::Java::Lang