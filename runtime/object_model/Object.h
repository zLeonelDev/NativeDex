#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>
#include <atomic>

namespace NativeDex::Runtime::ObjectModel {

class ClassObject;
class Object;

class Object {
public:
    // Object header layout matching ART/Dalvik
    ClassObject* klass = nullptr;
    std::atomic<uint32_t> lockWord{0};  // Thin lock + hash code + GC bits
    uint32_t gcMarkWord = 0; // GC mark bits, forwarding pointer, etc.

    // ---- Instance field storage (real runtime) -------------------------
    // One slot per instance field, laid out by the class registry
    // (superclass fields first). A slot holds either a primitive
    // (bits) or an object reference (ref), never both.
    struct FieldSlot {
        uint64_t bits = 0;
        Object* ref = nullptr;
    };
    std::vector<FieldSlot> fieldSlots;

    // Opaque handle into a native subsystem (e.g. the NativeViewRegistry id
    // of a View, or the raw bits of a boxed number).
    uint64_t nativeHandle = 0;

    // Optional native payload (list contents, StringBuilder buffer, ...).
    struct NativeData {
        virtual ~NativeData() = default;
    };
    std::unique_ptr<NativeData> native;

    template <typename T>
    T* nativeAs() const { return static_cast<T*>(native.get()); }

    // ---- Legacy sparse field maps (kept for older callers) --------------
    // Allocated lazily so objects that do not use them stay small.
    struct LegacyFields {
        std::unordered_map<uint64_t, uint32_t> intFields;
        std::unordered_map<uint64_t, uint64_t> wideFields;
        std::unordered_map<uint64_t, Object*> objFields;
    };
    std::unique_ptr<LegacyFields> legacy;
    LegacyFields& legacyFields() {
        if (!legacy) legacy = std::make_unique<LegacyFields>();
        return *legacy;
    }

    Object() = default;
    virtual ~Object() = default;

    // Field access by packed field ID
    uint32_t getIntField(uint64_t fieldId) const {
        if (!legacy) return 0;
        auto it = legacy->intFields.find(fieldId);
        return it != legacy->intFields.end() ? it->second : 0;
    }
    void setIntField(uint64_t fieldId, uint32_t val) { legacyFields().intFields[fieldId] = val; }

    uint64_t getWideField(uint64_t fieldId) const {
        if (!legacy) return 0;
        auto it = legacy->wideFields.find(fieldId);
        return it != legacy->wideFields.end() ? it->second : 0;
    }
    void setWideField(uint64_t fieldId, uint64_t val) { legacyFields().wideFields[fieldId] = val; }

    Object* getObjField(uint64_t fieldId) const {
        if (!legacy) return nullptr;
        auto it = legacy->objFields.find(fieldId);
        return it != legacy->objFields.end() ? it->second : nullptr;
    }
    void setObjField(uint64_t fieldId, Object* val) { legacyFields().objFields[fieldId] = val; }

    ClassObject* getClass() const { return klass; }
    void setClass(ClassObject* k) { klass = k; }

    // Identity hash code
    uint32_t getIdentityHashCode() const {
        return lockWord.load(std::memory_order_relaxed) & 0x3FFFFFFF; // Lower 30 bits
    }
    void setIdentityHashCode(uint32_t hash) {
        uint32_t old = lockWord.load(std::memory_order_relaxed);
        uint32_t newVal = (old & ~0x3FFFFFFF) | (hash & 0x3FFFFFFF);
        lockWord.store(newVal, std::memory_order_relaxed);
    }

    // Thin lock operations
    bool tryLock() {
        uint32_t old = lockWord.load(std::memory_order_relaxed);
        if ((old & 0xC0000000) != 0) return false; // Already locked or inflated
        uint32_t newLock = old | 0x40000000; // Set lock bit
        return lockWord.compare_exchange_strong(old, newLock, std::memory_order_acquire);
    }
    void unlock() {
        lockWord.fetch_and(~0x40000000, std::memory_order_release);
    }
    bool isLocked() const {
        return (lockWord.load(std::memory_order_relaxed) & 0x40000000) != 0;
    }

    // GC support
    bool isMarked() const { return (gcMarkWord & 1) != 0; }
    void setMarked(bool m) { gcMarkWord = (gcMarkWord & ~1) | (m ? 1 : 0); }
    
    // Object size (including header)
    virtual size_t objectSize() const = 0;
    
    // Visit object references for GC
    virtual void visitReferences(void (*visitor)(Object**)) = 0;
};

} // namespace NativeDex::Runtime::ObjectModel