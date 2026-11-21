#include "HeapManager.h"
#include "Object.h"
#include "ClassObject.h"
#include "ArrayObject.h"
#include "StringObject.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <malloc.h>

namespace NativeDex::Runtime::Heap {

HeapManager::HeapManager(size_t initialSize) 
    : totalBytes_(initialSize), nextGCThreshold_(initialSize / 2) {
    // Create initial block
    currentBlock_ = createNewBlock(initialSize);
    blocks_.push_back(std::unique_ptr<HeapBlock>(currentBlock_));
}

HeapManager::~HeapManager() {
    // Run finalizers
    processFinalizers();
}

HeapManager::HeapBlock* HeapManager::createNewBlock(size_t size) {
    HeapBlock* block = new HeapBlock();
    block->start = static_cast<uint8_t*>(_aligned_malloc(size, 8));
    block->end = block->start + size;
    block->current = block->start;
    block->size = size;
    block->isLargeObject = false;
    block->next = nullptr;
    return block;
}

HeapManager::HeapBlock* HeapManager::findBlockForSize(size_t size) {
    std::lock_guard<std::mutex> lock(blocksMutex_);
    for (auto& block : blocks_) {
        if (!block->isLargeObject && block->end - block->current >= static_cast<ptrdiff_t>(size)) {
            return block.get();
        }
    }
    return nullptr;
}

void* HeapManager::allocateFromBlock(HeapBlock* block, size_t size, size_t alignment) {
    uint8_t* ptr = block->current;
    uintptr_t aligned = (reinterpret_cast<uintptr_t>(ptr) + alignment - 1) & ~(alignment - 1);
    uint8_t* alignedPtr = reinterpret_cast<uint8_t*>(aligned);
    
    if (alignedPtr + size > block->end) {
        return nullptr;
    }
    
    block->current = alignedPtr + size;
    return alignedPtr;
}

void HeapManager::refillTLAB(TLAB* tlab) {
    if (!tlab) return;
    
    std::lock_guard<std::mutex> lock(blocksMutex_);
    HeapBlock* block = findBlockForSize(tlab->size);
    if (!block) {
        block = createNewBlock(tlab->size);
        blocks_.push_back(std::unique_ptr<HeapBlock>(block));
    }
    
    tlab->start = block->current;
    tlab->end = block->end;
    tlab->current = tlab->start;
    tlab->active = true;
}

Object* HeapManager::allocateObject(ClassObject* klass, size_t instanceSize) {
    size_t totalSize = sizeof(ObjectHeader) + instanceSize;
    totalSize = (totalSize + 7) & ~7; // 8-byte alignment
    
    // Try TLAB first
    // (Simplified: use current block)
    std::lock_guard<std::mutex> lock(blocksMutex_);
    
    void* ptr = allocateFromBlock(currentBlock_, totalSize, 8);
    if (!ptr) {
        // Try to find or create a new block
        currentBlock_ = createNewBlock(std::max<size_t>(totalSize, 1024 * 1024));
        blocks_.push_back(std::unique_ptr<HeapBlock>(currentBlock_));
        ptr = allocateFromBlock(currentBlock_, totalSize, 8);
    }
    
    if (!ptr) {
        throw std::bad_alloc();
    }
    
    // Initialize object header
    ObjectHeader* header = static_cast<ObjectHeader*>(ptr);
    header->klass = klass;
    header->markWord.store(0, std::memory_order_relaxed);
    header->instanceSize = static_cast<uint32_t>(instanceSize);
    
    Object* obj = reinterpret_cast<Object*>(reinterpret_cast<uint8_t*>(ptr) + sizeof(ObjectHeader));
    obj->setClass(klass);
    
    usedBytes_.fetch_add(totalSize, std::memory_order_relaxed);
    objectCount_.fetch_add(1, std::memory_order_relaxed);
    allocationsSinceGC_.fetch_add(1, std::memory_order_relaxed);
    
    // Trigger GC if needed
    if (allocationsSinceGC_.load() >= GC_INTERVAL) {
        allocationsSinceGC_.store(0);
        // Note: In real implementation, we'd schedule GC, not run it inline
    }
    
    return obj;
}

ObjectModel::ArrayObject* HeapManager::allocateArray(ObjectModel::ArrayObject::ArrayType type, size_t length) {
    size_t elementSize = 1;
    switch (type) {
        case ObjectModel::ArrayObject::ArrayType::BYTE:
        case ObjectModel::ArrayObject::ArrayType::BOOLEAN:
            elementSize = 1; break;
        case ObjectModel::ArrayObject::ArrayType::CHAR:
        case ObjectModel::ArrayObject::ArrayType::SHORT:
            elementSize = 2; break;
        case ObjectModel::ArrayObject::ArrayType::INT:
        case ObjectModel::ArrayObject::ArrayType::FLOAT:
            elementSize = 4; break;
        case ObjectModel::ArrayObject::ArrayType::LONG:
        case ObjectModel::ArrayObject::ArrayType::DOUBLE:
            elementSize = 8; break;
        case ObjectModel::ArrayObject::ArrayType::OBJECT:
            elementSize = sizeof(void*); break;
    }
    
    size_t dataSize = length * elementSize;
    size_t totalSize = sizeof(ObjectHeader) + sizeof(ObjectModel::ArrayObject) + dataSize;
    totalSize = (totalSize + 7) & ~7;
    
    std::lock_guard<std::mutex> lock(blocksMutex_);
    void* ptr = allocateFromBlock(currentBlock_, totalSize, 8);
    if (!ptr) {
        currentBlock_ = createNewBlock(std::max<size_t>(totalSize, 1024 * 1024));
        blocks_.push_back(std::unique_ptr<HeapBlock>(currentBlock_));
        ptr = allocateFromBlock(currentBlock_, totalSize, 8);
    }
    
    if (!ptr) throw std::bad_alloc();
    
    ObjectHeader* header = static_cast<ObjectHeader*>(ptr);
    header->klass = nullptr; // Array class set by ArrayObject constructor
    header->markWord.store(0, std::memory_order_relaxed);
    header->instanceSize = static_cast<uint32_t>(sizeof(ObjectModel::ArrayObject) + dataSize);
    
    ObjectModel::ArrayObject* array = new (reinterpret_cast<uint8_t*>(ptr) + sizeof(ObjectHeader)) 
        ObjectModel::ArrayObject(type, length);
    array->setClass(nullptr); // Will be set by class loader
    
    usedBytes_.fetch_add(totalSize, std::memory_order_relaxed);
    objectCount_.fetch_add(1, std::memory_order_relaxed);
    
    return array;
}

ObjectModel::ArrayObject* HeapManager::allocateObjectArray(ClassObject* componentType, size_t length) {
    auto* array = allocateArray(ObjectModel::ArrayObject::ArrayType::OBJECT, length);
    if (array) {
        array->setComponentClass(componentType);
    }
    return array;
}

void* HeapManager::allocateRaw(size_t size, size_t alignment) {
    size = (size + alignment - 1) & ~(alignment - 1);
    
    std::lock_guard<std::mutex> lock(blocksMutex_);
    void* ptr = allocateFromBlock(currentBlock_, size, alignment);
    if (!ptr) {
        currentBlock_ = createNewBlock(std::max<size_t>(size, 1024 * 1024));
        blocks_.push_back(std::unique_ptr<HeapBlock>(currentBlock_));
        ptr = allocateFromBlock(currentBlock_, size, alignment);
    }
    return ptr;
}

HeapManager::ObjectHeader* HeapManager::getHeader(void* ptr) {
    return reinterpret_cast<ObjectHeader*>(reinterpret_cast<uint8_t*>(ptr) - sizeof(ObjectHeader));
}

void HeapManager::markObject(Object* obj) {
    if (!obj) return;
    ObjectHeader* header = getHeader(obj);
    uint32_t old = header->markWord.load(std::memory_order_acquire);
    if ((old & ObjectHeader::MARKED_BIT) == 0) {
        header->markWord.fetch_or(ObjectHeader::MARKED_BIT, std::memory_order_release);
        // Recursively mark referenced objects
        // This is simplified - real impl would use work queue
    }
}

void HeapManager::sweep() {
    std::lock_guard<std::mutex> lock(blocksMutex_);
    
    for (auto& block : blocks_) {
        if (block->isLargeObject) continue;
        
        uint8_t* ptr = block->start;
        uint8_t* end = block->current;
        
        while (ptr < end) {
            ObjectHeader* header = reinterpret_cast<ObjectHeader*>(ptr);
            Object* obj = reinterpret_cast<Object*>(ptr + sizeof(ObjectHeader));
            
            if (header->markWord.load(std::memory_order_acquire) & ObjectHeader::MARKED_BIT) {
                // Marked - keep it, clear mark for next GC
                header->markWord.fetch_and(~ObjectHeader::MARKED_BIT, std::memory_order_release);
                ptr += sizeof(ObjectHeader) + header->instanceSize;
                ptr = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(ptr) + 7) & ~7);
            } else {
                // Not marked - free it
                // Call destructor if needed
                obj->~Object();
                // Add to free list (simplified: just skip)
                ptr += sizeof(ObjectHeader) + header->instanceSize;
                ptr = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(ptr) + 7) & ~7);
            }
        }
        block->current = ptr;
    }
}

void HeapManager::collectGarbage() {
    std::cout << "[GC] Starting garbage collection..." << std::endl;
    size_t before = usedBytes_.load();
    
    markRoots();
    markFromRoots();
    sweep();
    processWeakReferences();
    processFinalizers();
    
    size_t after = usedBytes_.load();
    std::cout << "[GC] Completed: " << before << " -> " << after 
              << " bytes (" << (before - after) << " freed)" << std::endl;
}

void HeapManager::markRoots() {
    std::lock_guard<std::mutex> lock(rootsMutex_);
    for (Object** root : roots_) {
        if (*root) {
            markObject(*root);
        }
    }
}

void HeapManager::markFromRoots() {
    // Work queue for iterative marking
    std::vector<Object*> workQueue;
    
    // Scan roots
    std::lock_guard<std::mutex> lock(rootsMutex_);
    for (Object** root : roots_) {
        if (*root) {
            ObjectHeader* header = getHeader(*root);
            if ((header->markWord.load() & ObjectHeader::MARKED_BIT) == 0) {
                header->markWord.fetch_or(ObjectHeader::MARKED_BIT);
                workQueue.push_back(*root);
            }
        }
    }
    
    // Process work queue
    while (!workQueue.empty()) {
        Object* obj = workQueue.back();
        workQueue.pop_back();
        scanObject(obj);
    }
}

void HeapManager::scanObject(Object* obj) {
    if (!obj) return;
    
    ClassObject* klass = obj->getClass();
    if (!klass) return;
    
    // Scan instance fields (simplified - would iterate actual fields)
    // For now, just handle arrays specially
    if (auto* array = dynamic_cast<ObjectModel::ArrayObject*>(obj)) {
        scanArray(array);
    }
}

void HeapManager::scanArray(ObjectModel::ArrayObject* array) {
    if (!array || !array->isObjectArray()) return;
    
    for (size_t i = 0; i < array->length(); ++i) {
        Object* elem = array->getObject(i);
        if (elem) {
            ObjectHeader* header = getHeader(elem);
            if ((header->markWord.load() & ObjectHeader::MARKED_BIT) == 0) {
                header->markWord.fetch_or(ObjectHeader::MARKED_BIT);
            }
        }
    }
}

void HeapManager::processWeakReferences() {
    std::lock_guard<std::mutex> lock(weakRefsMutex_);
    for (Object** ref : weakRefs_) {
        if (*ref) {
            ObjectHeader* header = getHeader(*ref);
            if ((header->markWord.load() & ObjectHeader::MARKED_BIT) == 0) {
                *ref = nullptr; // Clear weak reference
            }
        }
    }
}

void HeapManager::registerFinalizer(Object* obj) {
    std::lock_guard<std::mutex> lock(finalizationMutex_);
    finalizationQueue_.push_back(obj);
}

void HeapManager::processFinalizers() {
    std::lock_guard<std::mutex> lock(finalizationMutex_);
    for (Object* obj : finalizationQueue_) {
        ObjectHeader* header = getHeader(obj);
        if ((header->markWord.load() & ObjectHeader::MARKED_BIT) == 0) {
            // Object is dead but has finalizer - move to pending
            pendingFinalizers_.push_back(obj);
            header->markWord.fetch_or(ObjectHeader::FINALIZED_BIT);
        }
    }
    finalizationQueue_.clear();
    
    // Run pending finalizers (in real impl, would run on finalizer thread)
    for (Object* obj : pendingFinalizers_) {
        // Call finalize() method
        // obj->callMethod("finalize");
    }
    pendingFinalizers_.clear();
}

HeapManager::TLAB* HeapManager::createTLAB(size_t size) {
    std::lock_guard<std::mutex> lock(tlabMutex_);
    auto tlab = std::make_unique<TLAB>();
    tlab->size = size;
    tlabels_.push_back(std::move(tlab));
    return tlabels_.back().get();
}

void HeapManager::destroyTLAB(TLAB* tlab) {
    std::lock_guard<std::mutex> lock(tlabMutex_);
    for (auto it = tlabels_.begin(); it != tlabels_.end(); ++it) {
        if (it->get() == tlab) {
            tlabels_.erase(it);
            break;
        }
    }
}

void* HeapManager::allocateInTLAB(TLAB* tlab, size_t size, size_t alignment) {
    if (!tlab || !tlab->active) return nullptr;
    
    uintptr_t aligned = (reinterpret_cast<uintptr_t>(tlab->current) + alignment - 1) & ~(alignment - 1);
    uint8_t* alignedPtr = reinterpret_cast<uint8_t*>(aligned);
    
    if (alignedPtr + size > tlab->end) {
        return nullptr; // TLAB full
    }
    
    tlab->current = alignedPtr + size;
    return alignedPtr;
}

void HeapManager::addWeakReference(Object** ref) {
    std::lock_guard<std::mutex> lock(weakRefsMutex_);
    weakRefs_.push_back(ref);
}

void HeapManager::removeWeakReference(Object** ref) {
    std::lock_guard<std::mutex> lock(weakRefsMutex_);
    weakRefs_.erase(std::remove(weakRefs_.begin(), weakRefs_.end(), ref), weakRefs_.end());
}

} // namespace NativeDex::Runtime::Heap