#pragma once

#include <iostream>

namespace NativeDex::Runtime::GC {

class GarbageCollector {
public:
    GarbageCollector();
    ~GarbageCollector();

    // Trigger garbage collection (placeholder, does nothing)
    void collect();

    // Other GC-related methods can be added here as needed
};

} // namespace NativeDex::Runtime::GC