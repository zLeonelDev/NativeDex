#include "Vm.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace NativeDex::Runtime::BytecodeInterpreter {

void registerCoreNatives();

namespace {
Vm* g_vm = nullptr;
std::once_flag g_nativesOnce;

inline uint32_t readUleb(const uint8_t*& p, const uint8_t* end) {
    uint32_t result = 0;
    int shift = 0;
    while (p < end) {
        uint8_t b = *p++;
        result |= static_cast<uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
        if (shift >= 35) break;
    }
    return result;
}

uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
} // namespace

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
Kind kindOfDescriptor(char c) {
    switch (c) {
        case 'V': return Kind::Void;
        case 'J': return Kind::Long;
        case 'F': return Kind::Float;
        case 'D': return Kind::Double;
        case 'L': case '[': return Kind::Ref;
        default: return Kind::Int;
    }
}

MethodSig parseProto(const std::string& p) {
    MethodSig sig;
    size_t i = (!p.empty() && p[0] == '(') ? 1 : 0;
    while (i < p.size() && p[i] != ')') {
        char c = p[i];
        Kind k;
        if (c == 'L') {
            k = Kind::Ref;
            size_t e = p.find(';', i);
            i = (e == std::string::npos) ? p.size() : e + 1;
        } else if (c == '[') {
            k = Kind::Ref;
            while (i < p.size() && p[i] == '[') ++i;
            if (i < p.size() && p[i] == 'L') {
                size_t e = p.find(';', i);
                i = (e == std::string::npos) ? p.size() : e + 1;
            } else {
                ++i;
            }
        } else {
            k = kindOfDescriptor(c);
            ++i;
        }
        sig.params.push_back(k);
        sig.argSlots += (k == Kind::Long || k == Kind::Double) ? 2 : 1;
    }
    if (i + 1 < p.size()) sig.ret = kindOfDescriptor(p[i + 1]);
    return sig;
}

size_t stackRemaining() {
    thread_local uintptr_t lowLimit = 0;
    char probe;
    if (lowLimit == 0) {
#ifdef _WIN32
        ULONG_PTR lo = 0, hi = 0;
        GetCurrentThreadStackLimits(&lo, &hi);
        lowLimit = static_cast<uintptr_t>(lo);
#else
        pthread_attr_t attr;
        if (pthread_getattr_np(pthread_self(), &attr) == 0) {
            void* addr = nullptr;
            size_t size = 0;
            pthread_attr_getstack(&attr, &addr, &size);
            lowLimit = reinterpret_cast<uintptr_t>(addr);
            pthread_attr_destroy(&attr);
        }
#endif
        if (lowLimit == 0) lowLimit = reinterpret_cast<uintptr_t>(&probe) - (512u * 1024u);
    }
    uintptr_t here = reinterpret_cast<uintptr_t>(&probe);
    return here > lowLimit ? static_cast<size_t>(here - lowLimit) : 0;
}

Vm::ThreadState& Vm::ts() {
    static thread_local ThreadState state;
    return state;
}

Vm& Vm::get() { return *g_vm; }
Vm* Vm::tryGet() { return g_vm; }

void Vm::log(const std::string& s) const {
    std::cerr << s << std::endl;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
Vm::Vm(std::vector<const DexLoader::DexFile*> dexFiles, bool debug)
    : dexFiles_(std::move(dexFiles)), debug_(debug) {
    g_vm = this;
    std::call_once(g_nativesOnce, [] { registerCoreNatives(); });

    dexCtx_.resize(dexFiles_.size());
    for (size_t i = 0; i < dexFiles_.size(); ++i) {
        const DexLoader::DexFile* d = dexFiles_[i];
        DexCtx& c = dexCtx_[i];
        c.dex = d;
        c.index = static_cast<uint32_t>(i);
        if (!d) continue;
        c.types.assign(d->types.size(), nullptr);
        c.strings.assign(d->strings.size(), nullptr);
        c.methods.resize(d->methods.size());
        c.fields.resize(d->fields.size());
        for (uint32_t k = 0; k < d->classes.size(); ++k) {
            std::string_view desc = d->getClassDescriptor(k);
            classDefs_.emplace(desc, ClassLoc{static_cast<uint32_t>(i), k});
        }
    }

    // The ownership of the interpreter lock belongs to whoever drives the VM.
    clsObject = findClass("Ljava/lang/Object;");
    clsString = findClass("Ljava/lang/String;");
    clsClass = findClass("Ljava/lang/Class;");
    for (auto& kv : classes_) kv.second->klass = clsClass;
}

Vm::~Vm() {
    if (g_vm == this) g_vm = nullptr;
}

int Vm::dexIndexOf(const DexLoader::DexFile* dex) const {
    for (size_t i = 0; i < dexFiles_.size(); ++i) {
        if (dexFiles_[i] == dex) return static_cast<int>(i);
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Interpreter lock
// ---------------------------------------------------------------------------
void Vm::lock() {
    ThreadState& t = ts();
    if (t.lockDepth++ > 0) return;
    gilWaiters_.fetch_add(1);
    gil_.lock();
    gilWaiters_.fetch_sub(1);
    gilOwner_.store(std::this_thread::get_id());
}

void Vm::unlock() {
    ThreadState& t = ts();
    if (t.lockDepth <= 0) return;
    if (--t.lockDepth > 0) return;
    gilOwner_.store(std::thread::id());
    gil_.unlock();
}

int Vm::releaseAll() {
    ThreadState& t = ts();
    int saved = t.lockDepth;
    if (saved > 0) {
        t.lockDepth = 0;
        gilOwner_.store(std::thread::id());
        gil_.unlock();
    }
    return saved;
}

void Vm::reacquire(int depth) {
    if (depth <= 0) return;
    gilWaiters_.fetch_add(1);
    gil_.lock();
    gilWaiters_.fetch_sub(1);
    gilOwner_.store(std::this_thread::get_id());
    ts().lockDepth = depth;
}

void Vm::yieldLock() {
    ThreadState& t = ts();
    if (t.nativeDepth > 0 || t.lockDepth <= 0) return;
    if (gilWaiters_.load() == 0) return;
    int s = releaseAll();
    std::this_thread::yield();
    reacquire(s);
}

bool Vm::ownsLock() const {
    return gilOwner_.load() == std::this_thread::get_id();
}

void Vm::spawnThread(std::function<void()> fn) {
    auto* heapFn = new std::function<void()>(std::move(fn));
#ifdef _WIN32
    auto proc = [](LPVOID p) -> DWORD {
        auto* f = static_cast<std::function<void()>*>(p);
        (*f)();
        delete f;
        return 0;
    };
    HANDLE h = CreateThread(nullptr, 64u * 1024u * 1024u, proc, heapFn,
                            STACK_SIZE_PARAM_IS_A_RESOURCE_RESERVATION, nullptr);
    if (h) CloseHandle(h); else { (*heapFn)(); delete heapFn; }
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64u * 1024u * 1024u);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t th;
    auto proc = [](void* p) -> void* {
        auto* f = static_cast<std::function<void()>*>(p);
        (*f)();
        delete f;
        return nullptr;
    };
    if (pthread_create(&th, &attr, proc, heapFn) != 0) { (*heapFn)(); delete heapFn; }
    pthread_attr_destroy(&attr);
#endif
}

// ---------------------------------------------------------------------------
// Message queue
// ---------------------------------------------------------------------------
static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Vm::post(std::function<void()> fn, int64_t delayMs) {
    if (poster_) { poster_(std::move(fn), delayMs); return; }
    std::lock_guard<std::mutex> lk(queueMutex_);
    queue_.push_back(Task{nowMs() + (delayMs > 0 ? delayMs : 0), taskSeq_++, std::move(fn)});
}

bool Vm::hasPendingTasks() {
    std::lock_guard<std::mutex> lk(queueMutex_);
    return !queue_.empty();
}

size_t Vm::pumpQueue(size_t maxTasks) {
    size_t ran = 0;
    while (ran < maxTasks) {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lk(queueMutex_);
            int best = -1;
            int64_t now = nowMs();
            for (size_t i = 0; i < queue_.size(); ++i) {
                if (queue_[i].dueMs > now) continue;
                if (best < 0 || queue_[i].dueMs < queue_[best].dueMs ||
                    (queue_[i].dueMs == queue_[best].dueMs && queue_[i].seq < queue_[best].seq)) best = static_cast<int>(i);
            }
            if (best < 0) break;
            fn = std::move(queue_[best].fn);
            queue_.erase(queue_.begin() + best);
        }
        runGuarded(fn, "queued task");
        ++ran;
    }
    return ran;
}

// ---------------------------------------------------------------------------
// Monitors
// ---------------------------------------------------------------------------
void Vm::monitorEnter(Object* o) {
    if (!o) throwNpe("monitor-enter");
    auto self = std::this_thread::get_id();
    for (;;) {
        Monitor& m = monitors_[o];
        if (m.count == 0) { m.owner = self; m.count = 1; return; }
        if (m.owner == self) { m.count++; return; }
        // Owned by another thread: let it run.
        int s = releaseAll();
        std::this_thread::sleep_for(std::chrono::microseconds(200));
        reacquire(s);
    }
}

void Vm::monitorExit(Object* o) {
    if (!o) return;
    auto it = monitors_.find(o);
    if (it == monitors_.end()) return;
    if (it->second.count == 0 || it->second.owner != std::this_thread::get_id()) return;
    if (--it->second.count <= 0) {
        it->second.count = 0;
        it->second.owner = std::thread::id();
        if (it->second.waiters == 0) monitors_.erase(it);
    }
}

void Vm::monitorWait(Object* o, int64_t timeoutMs) {
    if (!o) throwNpe("wait");
    auto self = std::this_thread::get_id();
    auto it = monitors_.find(o);
    int saved = 0;
    if (it != monitors_.end() && it->second.count > 0 && it->second.owner == self) {
        saved = it->second.count;
        it->second.count = 0;
        it->second.owner = std::thread::id();
    }
    monitors_[o].waiters++;
    auto start = std::chrono::steady_clock::now();
    for (;;) {
        int s = releaseAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        reacquire(s);
        Monitor& m = monitors_[o];
        if (m.tokens > 0) { m.tokens--; break; }
        if (timeoutMs > 0 && std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(timeoutMs)) break;
    }
    monitors_[o].waiters--;
    if (saved > 0) {
        for (;;) {
            Monitor& m = monitors_[o];
            if (m.count == 0) { m.owner = self; m.count = saved; break; }
            int s = releaseAll();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            reacquire(s);
        }
    } else {
        auto it2 = monitors_.find(o);
        if (it2 != monitors_.end() && it2->second.count == 0 && it2->second.waiters == 0) monitors_.erase(it2);
    }
}

void Vm::monitorNotify(Object* o, bool all) {
    if (!o) return;
    auto it = monitors_.find(o);
    if (it == monitors_.end()) return;
    Monitor& m = it->second;
    if (all) m.tokens = m.waiters;
    else if (m.tokens < m.waiters) m.tokens++;
}

// ---------------------------------------------------------------------------
// Class loading
// ---------------------------------------------------------------------------
RuntimeClass* Vm::findDexClass(const std::string& descriptor) {
    auto it = classes_.find(descriptor);
    if (it != classes_.end()) return it->second->isNative() ? nullptr : it->second;
    auto loc = classDefs_.find(std::string_view(descriptor));
    if (loc == classDefs_.end()) return nullptr;
    return findClass(descriptor);
}

RuntimeClass* Vm::findClass(const std::string& descriptor) {
    auto it = classes_.find(descriptor);
    if (it != classes_.end()) return it->second;

    if (!descriptor.empty() && descriptor[0] == '[') return makeArrayClass(descriptor);

    auto loc = classDefs_.find(std::string_view(descriptor));
    if (loc != classDefs_.end()) return loadDexClass(loc->second.dex, loc->second.def);
    return makeNativeClass(descriptor);
}

RuntimeClass* Vm::classByType(uint32_t dexIndex, uint32_t typeIdx) {
    if (dexIndex >= dexCtx_.size()) return nullptr;
    DexCtx& c = dexCtx_[dexIndex];
    if (typeIdx >= c.types.size()) return nullptr;
    RuntimeClass*& slot = c.types[typeIdx];
    if (!slot) slot = findClass(std::string(c.dex->getTypeDescriptor(typeIdx)));
    return slot;
}

RuntimeClass* Vm::makeNativeClass(const std::string& descriptor) {
    auto* c = new RuntimeClass(descriptor);
    c->state = RuntimeClass::State::Initialized;
    classes_[descriptor] = c;
    c->klass = clsClass;
    c->isPrimitiveClass = descriptor.size() == 1;

    auto& reg = NativeRegistry::instance();
    const NativeRegistry::ClassDecl* d = reg.findDecl(descriptor);
    std::string super;
    if (d) {
        super = d->super;
        c->declaredInterface = d->isInterface;
        if (d->isInterface) c->setAccessFlags(ACC_INTERFACE | ACC_ABSTRACT | ACC_PUBLIC);
    } else if (descriptor != "Ljava/lang/Object;" && !c->isPrimitiveClass) {
        // Heuristic for framework classes we have not declared explicitly.
        auto ends = [&](const char* suf) {
            size_t n = std::strlen(suf);
            return descriptor.size() > n + 1 &&
                   descriptor.compare(descriptor.size() - 1 - n, n, suf) == 0;
        };
        if (ends("Exception")) super = "Ljava/lang/Exception;";
        else if (ends("Error")) super = "Ljava/lang/Error;";
        else super = "Ljava/lang/Object;";
    }
    if (descriptor == "Ljava/lang/Object;") super.clear();
    if (!super.empty()) {
        c->parent = findClass(super);
        c->setSuperClass(c->parent);
    }
    if (d) {
        for (const auto& i : d->interfaces) {
            RuntimeClass* ic = findClass(i);
            c->ifaces.push_back(ic);
            c->addInterface(ic);
        }
    }
    return c;
}

RuntimeClass* Vm::makeArrayClass(const std::string& descriptor) {
    auto* c = new RuntimeClass(descriptor);
    c->state = RuntimeClass::State::Initialized;
    c->isArrayClass = true;
    classes_[descriptor] = c;
    c->klass = clsClass;
    c->component = findClass(descriptor.substr(1));
    c->parent = findClass("Ljava/lang/Object;");
    c->setSuperClass(c->parent);
    c->ifaces.push_back(findClass("Ljava/lang/Cloneable;"));
    c->ifaces.push_back(findClass("Ljava/io/Serializable;"));
    return c;
}

std::string Vm::protoString(const DexLoader::DexFile* dex, uint32_t protoIdx) const {
    if (!dex || protoIdx >= dex->protos.size()) return "()V";
    const auto& p = dex->protos.getProto(protoIdx);
    std::string out = "(";
    const auto& raw = dex->rawData;
    if (p.parametersOff != 0 && static_cast<size_t>(p.parametersOff) + 4 <= raw.size()) {
        uint32_t n = 0;
        std::memcpy(&n, raw.data() + p.parametersOff, 4);
        size_t off = p.parametersOff + 4;
        for (uint32_t i = 0; i < n; ++i) {
            if (off + 2 > raw.size()) break;
            uint16_t t = 0;
            std::memcpy(&t, raw.data() + off, 2);
            off += 2;
            out += std::string(dex->getTypeDescriptor(t));
        }
    }
    out += ")";
    out += std::string(dex->getTypeDescriptor(p.returnTypeIdx));
    return out;
}

RuntimeClass* Vm::loadDexClass(uint32_t dexIndex, uint32_t classDefIdx) {
    const DexLoader::DexFile* dex = dexFiles_[dexIndex];
    const auto& def = dex->classes.getClassDef(classDefIdx);
    std::string desc(dex->getTypeDescriptor(def.classIdx));

    auto* c = new RuntimeClass(desc);
    c->dex = dex;
    c->dexIndex = dexIndex;
    c->classDefIdx = static_cast<int32_t>(classDefIdx);
    c->setAccessFlags(def.accessFlags);
    c->state = RuntimeClass::State::Loaded;
    c->klass = clsClass;
    classes_[desc] = c;
    dexCtx_[dexIndex].types[def.classIdx] = c;

    // Superclass and interfaces.
    if (def.superclassIdx != 0xFFFFFFFFu && def.superclassIdx < dex->types.size()) {
        c->parent = classByType(dexIndex, def.superclassIdx);
        c->setSuperClass(c->parent);
    } else if (desc != "Ljava/lang/Object;") {
        c->parent = findClass("Ljava/lang/Object;");
        c->setSuperClass(c->parent);
    }
    const auto& raw = dex->rawData;
    if (def.interfacesOff != 0 && static_cast<size_t>(def.interfacesOff) + 4 <= raw.size()) {
        uint32_t n = 0;
        std::memcpy(&n, raw.data() + def.interfacesOff, 4);
        size_t off = def.interfacesOff + 4;
        for (uint32_t i = 0; i < n && off + 2 <= raw.size(); ++i, off += 2) {
            uint16_t t = 0;
            std::memcpy(&t, raw.data() + off, 2);
            RuntimeClass* ic = classByType(dexIndex, t);
            if (ic) { c->ifaces.push_back(ic); c->addInterface(ic); }
        }
    }

    // class_data_item
    if (def.classDataOff != 0 && static_cast<size_t>(def.classDataOff) < raw.size()) {
        const uint8_t* p = raw.data() + def.classDataOff;
        const uint8_t* end = raw.data() + raw.size();
        uint32_t sCount = readUleb(p, end);
        uint32_t iCount = readUleb(p, end);
        uint32_t dCount = readUleb(p, end);
        uint32_t vCount = readUleb(p, end);

        auto readFields = [&](uint32_t count, bool isStatic) {
            uint32_t idx = 0;
            for (uint32_t i = 0; i < count; ++i) {
                idx += readUleb(p, end);
                uint32_t flags = readUleb(p, end);
                if (idx >= dex->fields.size()) continue;
                RField f;
                f.name = std::string(dex->getFieldName(idx));
                f.type = std::string(dex->getFieldType(idx));
                f.flags = flags;
                f.isStatic = isStatic;
                f.kind = f.type.empty() ? Kind::Int : kindOfDescriptor(f.type[0]);
                f.declaring = c;
                c->fieldIndex[f.name + " " + f.type] = static_cast<uint32_t>(c->fields.size());
                c->fields.push_back(std::move(f));
            }
        };
        readFields(sCount, true);
        readFields(iCount, false);

        c->methods.reserve(static_cast<size_t>(dCount) + vCount);
        auto readMethods = [&](uint32_t count) {
            uint32_t idx = 0;
            for (uint32_t i = 0; i < count; ++i) {
                idx += readUleb(p, end);
                uint32_t flags = readUleb(p, end);
                uint32_t codeOff = readUleb(p, end);
                if (idx >= dex->methods.size()) continue;
                const auto& mid = dex->methods.getMethod(idx);
                RMethod m;
                m.name = std::string(dex->strings.getString(mid.nameIdx));
                m.proto = protoString(dex, mid.protoIdx);
                m.key = m.name + m.proto;
                m.flags = flags;
                m.dex = dex;
                m.dexIndex = dexIndex;
                m.methodIdx = idx;
                m.owner = c;
                m.sig = parseProto(m.proto);
                if (codeOff != 0 && idx < dex->codeItems.size() && !dex->codeItems[idx].insns.empty()) {
                    m.code = &dex->codeItems[idx];
                }
                c->methodIndex[m.key] = static_cast<uint32_t>(c->methods.size());
                c->methods.push_back(std::move(m));
            }
        };
        readMethods(dCount);
        readMethods(vCount);
    }

    linkInstanceLayout(c);
    return c;
}

void Vm::linkInstanceLayout(RuntimeClass* c) {
    uint32_t inst = c->parent ? c->parent->instanceSlots : 0;
    uint32_t stat = 0;
    for (auto& f : c->fields) {
        if (f.isStatic) f.slot = stat++;
        else f.slot = inst++;
    }
    c->instanceSlots = inst;
    c->statics.assign(stat, Object::FieldSlot{});
}

// ---- encoded_array (static initial values) ----------------------------------
namespace {

uint64_t readSized(const uint8_t*& p, const uint8_t* end, int size, bool signExtend) {
    uint64_t v = 0;
    for (int i = 0; i < size && p < end; ++i) v |= static_cast<uint64_t>(*p++) << (8 * i);
    if (signExtend && size > 0 && size < 8) {
        uint64_t sign = 1ULL << (size * 8 - 1);
        if (v & sign) v |= ~((1ULL << (size * 8)) - 1);
    }
    return v;
}

void skipEncodedValue(const uint8_t*& p, const uint8_t* end);

void skipEncodedArray(const uint8_t*& p, const uint8_t* end) {
    uint32_t n = readUleb(p, end);
    for (uint32_t i = 0; i < n && p < end; ++i) skipEncodedValue(p, end);
}

void skipEncodedValue(const uint8_t*& p, const uint8_t* end) {
    if (p >= end) return;
    uint8_t h = *p++;
    uint8_t type = h & 0x1F;
    uint8_t arg = h >> 5;
    switch (type) {
        case 0x1C: skipEncodedArray(p, end); break;
        case 0x1D: {
            readUleb(p, end);
            uint32_t n = readUleb(p, end);
            for (uint32_t i = 0; i < n && p < end; ++i) { readUleb(p, end); skipEncodedValue(p, end); }
            break;
        }
        case 0x1E: case 0x1F: break;
        default: p += arg + 1; break;
    }
}

} // namespace

void Vm::initStatics(RuntimeClass* c) {
    if (!c->dex || c->classDefIdx < 0) return;
    const auto& def = c->dex->classes.getClassDef(static_cast<uint32_t>(c->classDefIdx));
    const auto& raw = c->dex->rawData;
    if (def.staticValuesOff == 0 || static_cast<size_t>(def.staticValuesOff) >= raw.size()) return;

    // Static fields in declaration order.
    std::vector<const RField*> statics;
    for (const auto& f : c->fields) if (f.isStatic) statics.push_back(&f);

    const uint8_t* p = raw.data() + def.staticValuesOff;
    const uint8_t* end = raw.data() + raw.size();
    uint32_t n = readUleb(p, end);
    for (uint32_t i = 0; i < n && i < statics.size() && p < end; ++i) {
        uint8_t h = *p++;
        uint8_t type = h & 0x1F;
        uint8_t arg = h >> 5;
        Object::FieldSlot& slot = c->statics[statics[i]->slot];
        switch (type) {
            case 0x00: slot.bits = static_cast<uint32_t>(readSized(p, end, arg + 1, true)); break;
            case 0x02: slot.bits = static_cast<uint32_t>(readSized(p, end, arg + 1, true)); break;
            case 0x03: slot.bits = static_cast<uint32_t>(readSized(p, end, arg + 1, false)); break;
            case 0x04: slot.bits = static_cast<uint32_t>(readSized(p, end, arg + 1, true)); break;
            case 0x06: slot.bits = readSized(p, end, arg + 1, true); break;
            case 0x10: slot.bits = static_cast<uint32_t>(readSized(p, end, arg + 1, false) << (8 * (3 - arg))); break;
            case 0x11: slot.bits = readSized(p, end, arg + 1, false) << (8 * (7 - arg)); break;
            case 0x17: {
                uint32_t idx = static_cast<uint32_t>(readSized(p, end, arg + 1, false));
                slot.ref = internedString(c->dexIndex, idx);
                break;
            }
            case 0x18: {
                uint32_t idx = static_cast<uint32_t>(readSized(p, end, arg + 1, false));
                slot.ref = classByType(c->dexIndex, idx);
                break;
            }
            case 0x1E: break;
            case 0x1F: slot.bits = arg ? 1 : 0; break;
            default:
                // method/field/enum/array/annotation: not valid as plain static
                // constants for our purposes; skip their payload.
                p -= 1;
                skipEncodedValue(p, end);
                break;
        }
    }
}

void Vm::ensureInitialized(RuntimeClass* c) {
    if (!c || c->state == RuntimeClass::State::Initialized) return;
    if (c->isNative()) { c->state = RuntimeClass::State::Initialized; return; }

    auto self = std::this_thread::get_id();
    if (c->state == RuntimeClass::State::Initializing) {
        if (c->initThread == self) return;
        while (c->state == RuntimeClass::State::Initializing) {
            int s = releaseAll();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            reacquire(s);
        }
        if (c->state == RuntimeClass::State::Initialized) return;
    }
    if (c->state == RuntimeClass::State::Failed) {
        throwNew("Ljava/lang/NoClassDefFoundError;", "Could not initialize class " + c->getClassName());
    }

    c->state = RuntimeClass::State::Initializing;
    c->initThread = self;
    try {
        if (c->parent) ensureInitialized(c->parent);
        initStatics(c);
        const RMethod* clinit = c->declaredMethod("<clinit>()V");
        if (clinit && clinit->hasCode()) invoke(clinit, nullptr, {});
        c->state = RuntimeClass::State::Initialized;
    } catch (JavaThrow& t) {
        c->state = RuntimeClass::State::Failed;
        if (debug_) log("[Vm] <clinit> failed for " + c->getClassName() + ": " + describeThrowable(t.ex));
        throw;
    }
}

bool Vm::isAssignable(const RuntimeClass* from, const RuntimeClass* to) const {
    if (!from || !to) return false;
    if (from == to) return true;
    if (to == clsObject) return true;
    if (from->isArrayClass) {
        if (to->isArrayClass) {
            const RuntimeClass* a = from->component;
            const RuntimeClass* b = to->component;
            if (!a || !b) return false;
            if (a->isPrimitiveClass || b->isPrimitiveClass) return a == b;
            return isAssignable(a, b);
        }
        const std::string& d = to->getDescriptor();
        return d == "Ljava/lang/Cloneable;" || d == "Ljava/io/Serializable;";
    }
    std::function<bool(const RuntimeClass*, int)> implements = [&](const RuntimeClass* k, int depth) -> bool {
        if (depth > 16) return false;
        for (const RuntimeClass* i : k->ifaces) {
            if (i == to || implements(i, depth + 1)) return true;
        }
        return false;
    };
    for (const RuntimeClass* k = from; k; k = k->parent) {
        if (k == to) return true;
        if (to->isInterfaceClass() && implements(k, 0)) return true;
    }
    return false;
}

bool Vm::isInstance(Object* o, const RuntimeClass* c) const {
    if (!o) return false;
    const RuntimeClass* k = static_cast<const RuntimeClass*>(o->klass);
    if (!k) return c == clsObject;
    return isAssignable(k, c);
}

RuntimeClass* Vm::classOf(Object* o) {
    if (!o) return nullptr;
    return o->klass ? static_cast<RuntimeClass*>(o->klass) : clsObject;
}

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------
Object* Vm::newInstance(RuntimeClass* c) {
    if (!c) c = clsObject;
    if (c == clsString) return newString(std::string());
    auto* o = new InstanceObject();
    o->klass = c;
    if (c->instanceSlots) o->fieldSlots.resize(c->instanceSlots);
    return o;
}

StringObject* Vm::newString(const std::string& utf8) {
    StringObject* s = StringObject::create(utf8);
    s->klass = clsString;
    return s;
}

StringObject* Vm::intern(const std::string& utf8) {
    auto it = interned_.find(utf8);
    if (it != interned_.end()) return it->second;
    StringObject* s = newString(utf8);
    interned_.emplace(utf8, s);
    return s;
}

Object* Vm::internedString(uint32_t dexIndex, uint32_t idx) {
    DexCtx& c = dexCtx_[dexIndex];
    if (idx >= c.strings.size()) return nullptr;
    StringObject*& slot = c.strings[idx];
    if (!slot) slot = intern(std::string(c.dex->strings.getString(idx)));
    return slot;
}

ArrayObject* Vm::newPrimitiveArray(ArrayObject::ArrayType t, int32_t length) {
    static const char* const descs[] = {"[B", "[C", "[S", "[I", "[J", "[F", "[D", "[Z", "[Ljava/lang/Object;"};
    return newArray(descs[static_cast<int>(t)], length);
}

ArrayObject* Vm::newObjectArray(const std::string& componentDescriptor, int32_t length) {
    return newArray("[" + componentDescriptor, length);
}

ArrayObject* Vm::newArray(const std::string& arrayDescriptor, int32_t length) {
    if (length < 0) throwNew("Ljava/lang/NegativeArraySizeException;", std::to_string(length));
    using AT = ArrayObject::ArrayType;
    AT type = AT::OBJECT;
    size_t elem = sizeof(void*);
    switch (arrayDescriptor.size() > 1 ? arrayDescriptor[1] : 'L') {
        case 'B': type = AT::BYTE; elem = 1; break;
        case 'Z': type = AT::BOOLEAN; elem = 1; break;
        case 'C': type = AT::CHAR; elem = 2; break;
        case 'S': type = AT::SHORT; elem = 2; break;
        case 'I': type = AT::INT; elem = 4; break;
        case 'F': type = AT::FLOAT; elem = 4; break;
        case 'J': type = AT::LONG; elem = 8; break;
        case 'D': type = AT::DOUBLE; elem = 8; break;
        default: break;
    }
    size_t bytes = elem * static_cast<size_t>(length);
    void* mem = std::malloc(sizeof(ArrayObject) + bytes + 8);
    if (!mem) throwNew("Ljava/lang/OutOfMemoryError;", "array of " + std::to_string(length));
    std::memset(static_cast<uint8_t*>(mem) + sizeof(ArrayObject), 0, bytes);
    RuntimeClass* klass = findClass(arrayDescriptor);
    ArrayObject* a;
    if (type == AT::OBJECT) {
        a = new (mem) ArrayObject(klass->component, static_cast<size_t>(length));
    } else {
        a = new (mem) ArrayObject(type, static_cast<size_t>(length));
    }
    a->klass = klass;
    return a;
}

Object* Vm::box(const std::string& boxClass, uint64_t bits) {
    Object* o = newInstance(findClass(boxClass));
    o->nativeHandle = bits;
    return o;
}

// ---------------------------------------------------------------------------
// Exceptions
// ---------------------------------------------------------------------------
ThrowableData* Vm::throwableData(Object* o) {
    if (!o) return nullptr;
    if (!o->native) o->native = std::make_unique<ThrowableData>();
    return o->nativeAs<ThrowableData>();
}

std::vector<std::string> Vm::captureStack() const {
    std::vector<std::string> out;
    int n = 0;
    for (Frame* f = ts().top; f && n < 64; f = f->previous, ++n) {
        if (!f->method) continue;
        out.push_back(f->method->owner->getClassName() + "." + f->method->name + f->method->proto +
                      " pc=" + std::to_string(f->pc));
    }
    return out;
}

Object* Vm::makeThrowable(const std::string& descriptor, const std::string& message) {
    Object* ex = newInstance(findClass(descriptor));
    ThrowableData* d = throwableData(ex);
    if (!message.empty()) d->message = newString(message);
    d->trace = captureStack();
    return ex;
}

void Vm::throwNew(const std::string& descriptor, const std::string& message) {
    throw JavaThrow{makeThrowable(descriptor, message)};
}

void Vm::throwNpe(const std::string& what) {
    throwNew("Ljava/lang/NullPointerException;", what);
}

std::string Vm::describeThrowable(Object* ex) {
    if (!ex) return "null";
    std::string s = classOf(ex)->getClassName();
    if (auto* d = ex->nativeAs<ThrowableData>()) {
        if (d->message) s += ": " + stringOf(d->message);
        for (size_t i = 0; i < d->trace.size() && i < 12; ++i) s += "\n    at " + d->trace[i];
        if (d->cause && d->cause != ex) s += "\n  caused by " + describeThrowable(d->cause);
    }
    return s;
}

// ---------------------------------------------------------------------------
// Method / field references
// ---------------------------------------------------------------------------
MethodRef& Vm::methodRef(uint32_t dexIndex, uint32_t idx) {
    DexCtx& c = dexCtx_[dexIndex];
    auto& slot = c.methods[idx];
    if (!slot) {
        slot = std::make_unique<MethodRef>();
        const auto& m = c.dex->methods.getMethod(idx);
        slot->cls = std::string(c.dex->getTypeDescriptor(m.classIdx));
        slot->name = std::string(c.dex->strings.getString(m.nameIdx));
        slot->proto = protoString(c.dex, m.protoIdx);
        slot->key = slot->name + slot->proto;
        slot->sig = parseProto(slot->proto);
    }
    return *slot;
}

FieldRef& Vm::fieldRef(uint32_t dexIndex, uint32_t idx) {
    DexCtx& c = dexCtx_[dexIndex];
    auto& slot = c.fields[idx];
    if (!slot) {
        slot = std::make_unique<FieldRef>();
        const auto& f = c.dex->fields.getField(idx);
        slot->cls = std::string(c.dex->getTypeDescriptor(f.classIdx));
        slot->name = std::string(c.dex->strings.getString(f.nameIdx));
        slot->type = std::string(c.dex->getTypeDescriptor(f.typeIdx));
        slot->kind = slot->type.empty() ? Kind::Int : kindOfDescriptor(slot->type[0]);
        slot->legacyKey = fnv1a(slot->name + ":" + slot->type);
    }
    return *slot;
}

void Vm::resolveField(FieldRef& fr) {
    if (fr.resolved) return;
    RuntimeClass* start = findClass(fr.cls);
    std::function<const RField*(RuntimeClass*, int)> search = [&](RuntimeClass* k, int depth) -> const RField* {
        if (!k || depth > 16) return nullptr;
        if (const RField* f = k->declaredField(fr.name, fr.type)) return f;
        for (RuntimeClass* i : k->ifaces) {
            if (const RField* f = search(i, depth + 1)) return f;
        }
        return search(k->parent, depth + 1);
    };
    if (const RField* f = search(start, 0)) {
        fr.declaring = f->declaring;
        fr.isStatic = f->isStatic;
        fr.slot = f->slot;
        fr.nativeField = false;
    } else {
        fr.declaring = start;
        fr.nativeField = true;
    }
    fr.resolved = true;
}

Object::FieldSlot& Vm::nativeStaticSlot(FieldRef& fr) {
    RuntimeClass* c = fr.declaring;
    auto it = c->nativeStatics.find(fr.name);
    if (it != c->nativeStatics.end()) return c->statics[it->second];

    uint32_t idx = static_cast<uint32_t>(c->statics.size());
    c->statics.emplace_back();
    c->nativeStatics[fr.name] = idx;
    Object::FieldSlot& slot = c->statics[idx];

    int64_t iv = 0;
    if (NativeRegistry::instance().getStaticInt(fr.cls, fr.name, iv)) {
        slot.bits = static_cast<uint64_t>(iv);
    } else if (std::string sv; NativeRegistry::instance().getStaticString(fr.cls, fr.name, sv)) {
        slot.ref = intern(sv);
    } else if (fr.type == fr.cls && c->isNative() && c->parent && c->parent->getDescriptor() == "Ljava/lang/Enum;") {
        Object* e = newInstance(c);
        auto* d = new EnumData();
        d->name = fr.name;
        d->ordinal = NativeRegistry::instance().enumOrdinal(fr.cls, fr.name);
        e->native.reset(d);
        slot.ref = e;
    } else if (fr.cls == "Ljava/lang/System;" && (fr.name == "out" || fr.name == "err")) {
        Object* ps = newInstance(findClass("Ljava/io/PrintStream;"));
        ps->nativeHandle = fr.name == "out" ? 1 : 2;
        slot.ref = ps;
    } else if (fr.cls == "Ljava/lang/Boolean;" && fr.name == "TRUE") {
        slot.ref = box("Ljava/lang/Boolean;", 1);
    } else if (fr.cls == "Ljava/lang/Boolean;" && fr.name == "FALSE") {
        slot.ref = box("Ljava/lang/Boolean;", 0);
    } else if (fr.cls == "Ljava/lang/Integer;" && fr.name == "MAX_VALUE") {
        slot.bits = 0x7FFFFFFFu;
    } else if (fr.cls == "Ljava/lang/Integer;" && fr.name == "MIN_VALUE") {
        slot.bits = 0x80000000u;
    } else if (fr.cls == "Ljava/lang/Long;" && fr.name == "MAX_VALUE") {
        slot.bits = 0x7FFFFFFFFFFFFFFFULL;
    } else if (fr.cls == "Ljava/lang/Long;" && fr.name == "MIN_VALUE") {
        slot.bits = 0x8000000000000000ULL;
    }
    return slot;
}

// ---------------------------------------------------------------------------
// Method lookup & invocation
// ---------------------------------------------------------------------------
// Value returned by a framework method we do not implement. Primitives and
// most references become zero/null; Android framework objects become a
// generic stub instance so that chained calls (getWindow().getDecorView()...)
// keep resolving to harmless defaults instead of raising NullPointerException.
Value Vm::stubReturn(const std::string& proto) {
    size_t p = proto.find(')');
    if (p == std::string::npos || p + 1 >= proto.size()) return Value();
    std::string ret = proto.substr(p + 1);
    if (ret.size() > 9 && ret.compare(0, 9, "Landroid/") == 0) {
        return Value::fromRef(newInstance(findClass(ret)));
    }
    return Value();
}

void Vm::stubMiss(const std::string& cls, const std::string& name, const std::string& proto) {
    std::string line = cls + "->" + name + " " + proto;
    NativeRegistry::instance().logMiss(line);
    if (debug_) log("[Vm] unimplemented: " + line);
}

Target Vm::lookupInterfaces(RuntimeClass* c, const std::string& key, const std::string& name,
                            const std::string& proto, int depth) {
    if (depth > 10) return {};
    auto& reg = NativeRegistry::instance();
    for (RuntimeClass* i : c->ifaces) {
        if (!i->isNative()) {
            const RMethod* m = i->declaredMethod(key);
            if (m && m->hasCode()) return Target{m, nullptr};
        } else if (const NativeFn* n = reg.find(i->getDescriptor(), name, proto)) {
            return Target{nullptr, n};
        }
        Target t = lookupInterfaces(i, key, name, proto, depth + 1);
        if (t.valid()) return t;
    }
    return {};
}

Target Vm::lookupFrom(RuntimeClass* start, const std::string& key, const std::string& name,
                      const std::string& proto, bool /*skipAbstract*/) {
    auto& reg = NativeRegistry::instance();
    for (RuntimeClass* c = start; c; c = c->parent) {
        if (!c->isNative()) {
            const RMethod* m = c->declaredMethod(key);
            if (m && m->hasCode()) return Target{m, nullptr};
            if (m && !m->isAbstract()) {
                // JNI-declared method: the registry may implement it.
                if (const NativeFn* n = reg.find(c->getDescriptor(), name, proto)) return Target{nullptr, n};
            }
        } else if (const NativeFn* n = reg.find(c->getDescriptor(), name, proto)) {
            return Target{nullptr, n};
        }
    }
    for (RuntimeClass* c = start; c; c = c->parent) {
        Target t = lookupInterfaces(c, key, name, proto, 0);
        if (t.valid()) return t;
    }
    return {};
}

Target Vm::resolveVirtual(RuntimeClass* start, const std::string& key, const std::string& name,
                          const std::string& proto) {
    return lookupFrom(start, key, name, proto, true);
}

const RMethod* Vm::findMethod(RuntimeClass* c, const std::string& name, const std::string& proto) {
    std::string key = name + proto;
    for (RuntimeClass* k = c; k; k = k->parent) {
        if (!k->isNative()) {
            if (const RMethod* m = k->declaredMethod(key)) return m;
        }
    }
    return nullptr;
}

Value Vm::invokeTarget(const Target& t, const MethodRef* ref, Object* self,
                       const std::vector<Value>& args) {
    if (t.method) return invoke(t.method, self, args);
    if (t.native) {
        NativeCall nc(*this);
        nc.self = self;
        nc.args = args;
        nc.ref = ref;
        ThreadState& st = ts();
        st.nativeDepth++;
        try { (*t.native)(nc); } catch (...) { st.nativeDepth--; throw; }
        st.nativeDepth--;
        return nc.ret;
    }
    return Value();
}

Value Vm::invoke(const RMethod* m, Object* self, const std::vector<Value>& args) {
    if (!m->hasCode()) {
        auto& reg = NativeRegistry::instance();
        if (const NativeFn* n = reg.find(m->owner->getDescriptor(), m->name, m->proto)) {
            return invokeTarget(Target{nullptr, n}, nullptr, self, args);
        }
        if (m->isAbstract()) {
            throwNew("Ljava/lang/AbstractMethodError;", m->owner->getClassName() + "." + m->name);
        }
        stubMiss(m->owner->getDescriptor(), m->name, m->proto);
        return Value();
    }
    uint32_t ins = m->code->insSize;
    std::vector<Reg> regs(ins ? ins : 1);
    size_t r = 0;
    if (!m->isStatic()) {
        regs[r++].o = self;
    }
    for (size_t i = 0; i < m->sig.params.size() && i < args.size(); ++i) {
        Kind k = m->sig.params[i];
        if (r >= regs.size()) break;
        if (k == Kind::Ref) {
            regs[r++].o = args[i].ref;
        } else if (k == Kind::Long || k == Kind::Double) {
            regs[r].v = static_cast<uint32_t>(args[i].bits);
            if (r + 1 < regs.size()) regs[r + 1].v = static_cast<uint32_t>(args[i].bits >> 32);
            r += 2;
        } else {
            regs[r++].v = static_cast<uint32_t>(args[i].bits);
        }
    }
    return executeMethod(m, regs.data(), ins, ts().top);
}

Value Vm::callVirtual(Object* self, const std::string& name, const std::string& proto,
                      const std::vector<Value>& args) {
    if (!self) throwNpe(name);
    RuntimeClass* k = classOf(self);
    Target t = resolveVirtual(k, name + proto, name, proto);
    if (!t.valid()) {
        stubMiss(k->getDescriptor(), name, proto);
        return Value();
    }
    return invokeTarget(t, nullptr, self, args);
}

Value Vm::callStatic(const std::string& cls, const std::string& name, const std::string& proto,
                     const std::vector<Value>& args) {
    RuntimeClass* c = findClass(cls);
    ensureInitialized(c);
    Target t = lookupFrom(c, name + proto, name, proto, true);
    if (!t.valid()) {
        stubMiss(cls, name, proto);
        return Value();
    }
    return invokeTarget(t, nullptr, nullptr, args);
}

bool Vm::runGuarded(const std::function<void()>& fn, const std::string& what) {
    Locker lk(*this);
    ThreadState& st = ts();
    Frame* savedTop = st.top;
    try {
        fn();
        return true;
    } catch (JavaThrow& t) {
        st.top = savedTop;
        log("[Vm] Uncaught Java exception in " + what + ": " + describeThrowable(t.ex));
    } catch (const std::exception& e) {
        st.top = savedTop;
        log("[Vm] Internal error in " + what + ": " + e.what());
    }
    return false;
}

// ---------------------------------------------------------------------------
// Java-level helpers
// ---------------------------------------------------------------------------
std::string Vm::stringOf(Object* o) {
    auto* s = dynamic_cast<StringObject*>(o);
    return s ? s->toUtf8() : std::string();
}

int32_t Vm::identityHash(Object* o) {
    if (!o) return 0;
    uint32_t h = o->getIdentityHashCode();
    if (h == 0) {
        static std::atomic<uint32_t> counter{0x1000};
        h = (counter.fetch_add(0x9E37) * 2654435761u >> 2) & 0x3FFFFFFF;
        if (h == 0) h = 1;
        o->setIdentityHashCode(h);
    }
    return static_cast<int32_t>(h);
}

bool Vm::javaEquals(Object* a, Object* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (auto* sa = dynamic_cast<StringObject*>(a)) {
        auto* sb = dynamic_cast<StringObject*>(b);
        return sb && sa->equals(sb);
    }
    RuntimeClass* k = classOf(a);
    static const std::string key = std::string("equals") + "(Ljava/lang/Object;)Z";
    Target t = resolveVirtual(k, key, "equals", "(Ljava/lang/Object;)Z");
    if (!t.valid()) return false;
    return invokeTarget(t, nullptr, a, {Value::fromRef(b)}).i32() != 0;
}

int32_t Vm::javaHashCode(Object* o) {
    if (!o) return 0;
    if (auto* s = dynamic_cast<StringObject*>(o)) return s->hashCode();
    RuntimeClass* k = classOf(o);
    Target t = resolveVirtual(k, "hashCode()I", "hashCode", "()I");
    if (!t.valid()) return identityHash(o);
    return invokeTarget(t, nullptr, o, {}).i32();
}

std::string Vm::javaToString(Object* o) {
    if (!o) return "null";
    if (auto* s = dynamic_cast<StringObject*>(o)) return s->toUtf8();
    RuntimeClass* k = classOf(o);
    Target t = resolveVirtual(k, "toString()Ljava/lang/String;", "toString", "()Ljava/lang/String;");
    if (!t.valid()) return k->getClassName() + "@" + std::to_string(identityHash(o));
    Value r = invokeTarget(t, nullptr, o, {});
    return r.ref ? stringOf(r.ref) : std::string("null");
}

} // namespace NativeDex::Runtime::BytecodeInterpreter
