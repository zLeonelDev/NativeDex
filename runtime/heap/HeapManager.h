#pragma once

#include <cstdint>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <new>
#include <functional>
#include "../object_model/Object.h"
#include "../object_model/ClassObject.h"
#include "../object_model/ArrayObject.h"

namespace NativeDex::Runtime::Heap {

using ObjectModel::Object;
using ObjectModel::ClassObject;
using ObjectModel::ArrayObject;

class HeapManager {
public:
    explicit HeapManager(size_t initialSize = 64 * 1024 * 1024); // 64MB default
    ~HeapManager();

    // Non-copyable, movable
    HeapManager(const HeapManager&) = delete;
    HeapManager& operator=(const HeapManager&) = delete;
    HeapManager(HeapManager&&) = default;
    HeapManager& operator=(HeapManager&&) = default;

    // Allocate an object (includes object header + instance data)
    Object* allocateObject(ClassObject* klass, size_t instanceSize);
    
    // Allocate array
    ArrayObject* allocateArray(ArrayObject::ArrayType type, size_t length);
    ArrayObject* allocateObjectArray(ClassObject* componentType, size_t length);

    // Allocate raw memory (for internal use)
    void* allocateRaw(size_t size, size_t alignment = 8);

    struct ObjectHeader;

    // Get allocated object header from pointer
    ObjectHeader* getHeader(void* ptr);

    // Memory stats
    size_t getUsedBytes() const noexcept { return usedBytes_.load(); }
    size_t getTotalBytes() const noexcept { return totalBytes_; }
    size_t getFreeBytes() const noexcept { return totalBytes_ - usedBytes_.load(); }
    size_t getObjectCount() const noexcept { return objectCount_.load(); }

    // GC support
    void markObject(Object* obj);
    void sweep();
    void collectGarbage();

    // Thread-local allocation buffers (TLABs)
    struct TLAB {
        uint8_t* start = nullptr;
        uint8_t* end = nullptr;
        uint8_t* current = nullptr;
        size_t size = 0;
        bool active = false;
    };
    
    TLAB* createTLAB(size_t size = 64 * 1024);
    void destroyTLAB(TLAB* tlab);
    void* allocateInTLAB(TLAB* tlab, size_t size, size_t alignment);

    // Weak references
    void addWeakReference(Object** ref);
    void removeWeakReference(Object** ref);
    void processWeakReferences();

    // Finalization
    void registerFinalizer(Object* obj);
    void processFinalizers();

public:
    struct ObjectHeader {
        ClassObject* klass = nullptr;
        std::atomic<uint32_t> markWord{0}; // GC mark bits + lock state + hash
        uint32_t instanceSize = 0;
        
        // Mark bits
        static constexpr uint32_t MARKED_BIT = 1 << 0;
        static constexpr uint32_t MARKING_BIT = 1 << 1;
        static constexpr uint32_t FINALIZED_BIT = 1 << 2;
    };

private:

    struct HeapBlock {
        uint8_t* start = nullptr;
        uint8_t* end = nullptr;
        uint8_t* current = nullptr;
        size_t size = 0;
        bool isLargeObject = false;
        HeapBlock* next = nullptr;
    };

    // Allocation
    void* allocateFromBlock(HeapBlock* block, size_t size, size_t alignment);
    HeapBlock* createNewBlock(size_t size);
    HeapBlock* findBlockForSize(size_t size);
    void refillTLAB(TLAB* tlab);

    // GC
    void markRoots();
    void markFromRoots();
    void markObjectInternal(Object* obj);
    void scanObject(Object* obj);
    void scanReference(Object** ref);
    void scanArray(ArrayObject* array);
    void sweepBlocks();
    void processPendingFinalizers();

    // Memory
    std::vector<std::unique_ptr<HeapBlock>> blocks_;
    HeapBlock* currentBlock_ = nullptr;
    HeapBlock* largeObjectList_ = nullptr;
    std::mutex blocksMutex_;

    std::atomic<size_t> usedBytes_{0};
    std::atomic<size_t> objectCount_{0};
    size_t totalBytes_ = 0;
    size_t nextGCThreshold_ = 0;

    // TLABs
    std::mutex tlabMutex_;
    std::vector<std::unique_ptr<TLAB>> tlabels_;
    
    // GC roots
    std::vector<Object**> roots_;
    std::mutex rootsMutex_;
    
    // Weak references
    std::vector<Object**> weakRefs_;
    std::mutex weakRefsMutex_;

    // Finalization
    std::vector<Object*> finalizationQueue_;
    std::mutex finalizationMutex_;
    std::vector<Object*> pendingFinalizers_;

    // Allocation stats
    std::atomic<size_t> allocationsSinceGC_{0};
    static constexpr size_t GC_INTERVAL = 10000; // Run GC every N allocations
};

} // namespace NativeDex::Runtime::Heap