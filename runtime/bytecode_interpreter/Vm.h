#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../dex_loader/DexFile.h"
#include "../object_model/Object.h"
#include "../object_model/ClassObject.h"
#include "../object_model/StringObject.h"
#include "../object_model/ArrayObject.h"
#include "../object_model/InstanceObject.h"
#include "Frame.h"
#include "NativeRegistry.h"

namespace NativeDex::Runtime::BytecodeInterpreter {

using ObjectModel::ArrayObject;
using ObjectModel::ClassObject;
using ObjectModel::InstanceObject;
using ObjectModel::StringObject;

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------
enum class Kind : uint8_t { Void, Int, Long, Float, Double, Ref };

Kind kindOfDescriptor(char c);

struct MethodSig {
    std::vector<Kind> params;
    Kind ret = Kind::Void;
    uint32_t argSlots = 0;   // register slots taken by the parameters (receiver excluded)
};
MethodSig parseProto(const std::string& proto);

// Access flags
constexpr uint32_t ACC_PUBLIC = 0x0001;
constexpr uint32_t ACC_PRIVATE = 0x0002;
constexpr uint32_t ACC_STATIC = 0x0008;
constexpr uint32_t ACC_FINAL = 0x0010;
constexpr uint32_t ACC_SYNCHRONIZED = 0x0020;
constexpr uint32_t ACC_NATIVE = 0x0100;
constexpr uint32_t ACC_INTERFACE = 0x0200;
constexpr uint32_t ACC_ABSTRACT = 0x0400;
constexpr uint32_t ACC_CONSTRUCTOR = 0x10000;

class RuntimeClass;

struct RField {
    std::string name;
    std::string type;
    uint32_t flags = 0;
    Kind kind = Kind::Int;
    bool isStatic = false;
    uint32_t slot = 0;           // index into fieldSlots (instance) or statics (static)
    RuntimeClass* declaring = nullptr;
};

struct RMethod {
    std::string name;
    std::string proto;           // "(II)V"
    std::string key;             // name + proto
    uint32_t flags = 0;
    const DexLoader::DexFile* dex = nullptr;
    uint32_t dexIndex = 0;
    uint32_t methodIdx = 0;
    const DexLoader::CodeItem* code = nullptr;
    RuntimeClass* owner = nullptr;
    MethodSig sig;

    bool isStatic() const { return (flags & ACC_STATIC) != 0; }
    bool isAbstract() const { return (flags & ACC_ABSTRACT) != 0; }
    bool isSynchronized() const { return (flags & ACC_SYNCHRONIZED) != 0; }
    bool hasCode() const { return code != nullptr && !code->insns.empty(); }
};

class RuntimeClass : public ClassObject {
public:
    explicit RuntimeClass(const std::string& d) : ClassObject(d) {}

    enum class State : uint8_t { Loaded, Initializing, Initialized, Failed };

    const DexLoader::DexFile* dex = nullptr;
    uint32_t dexIndex = 0;
    int32_t classDefIdx = -1;

    bool isArrayClass = false;
    bool isPrimitiveClass = false;
    bool declaredInterface = false;       // for framework classes
    RuntimeClass* component = nullptr;    // arrays
    RuntimeClass* parent = nullptr;
    std::vector<RuntimeClass*> ifaces;

    std::vector<RField> fields;           // declared fields
    std::unordered_map<std::string, uint32_t> fieldIndex;   // "name type" -> index
    std::vector<RMethod> methods;         // declared methods (never resized after load)
    std::unordered_map<std::string, uint32_t> methodIndex;  // key -> index

    uint32_t instanceSlots = 0;           // total instance slots including superclasses
    std::vector<Object::FieldSlot> statics;
    std::unordered_map<std::string, uint32_t> nativeStatics;   // dynamic statics of framework classes

    State state = State::Loaded;
    std::thread::id initThread;

    bool isNative() const { return dex == nullptr; }
    bool isInterfaceClass() const { return declaredInterface || (getAccessFlags() & ACC_INTERFACE) != 0; }

    const RMethod* declaredMethod(const std::string& key) const {
        auto it = methodIndex.find(key);
        return it == methodIndex.end() ? nullptr : &methods[it->second];
    }
    const RField* declaredField(const std::string& name, const std::string& type) const {
        auto it = fieldIndex.find(name + " " + type);
        return it == fieldIndex.end() ? nullptr : &fields[it->second];
    }
};

struct Target {
    const RMethod* method = nullptr;     // interpreted implementation
    const NativeFn* native = nullptr;    // native implementation
    bool valid() const { return method != nullptr || native != nullptr; }
};

// Symbolic method reference ("method_id") resolved lazily and cached.
struct MethodRef {
    std::string cls, name, proto, key;
    MethodSig sig;
    RuntimeClass* declared = nullptr;
    // inline cache for virtual/interface dispatch
    RuntimeClass* cacheKlass = nullptr;
    Target cacheTarget;
    // cached target for static/direct/super resolution
    bool directResolved = false;
    Target directTarget;
};

struct FieldRef {
    std::string cls, name, type;
    Kind kind = Kind::Int;
    bool resolved = false;
    bool isStatic = false;
    bool nativeField = false;      // field of a framework class
    RuntimeClass* declaring = nullptr;
    uint32_t slot = 0;
    uint64_t legacyKey = 0;
};

// Native state attached to every java.lang.Throwable instance.
struct ThrowableData : ObjectModel::Object::NativeData {
    Object* message = nullptr;
    Object* cause = nullptr;
    std::vector<std::string> trace;
};

// Native state of java.lang.Enum instances.
struct EnumData : ObjectModel::Object::NativeData {
    std::string name;
    int32_t ordinal = 0;
};

// A thrown Java exception travelling through C++ frames.
struct JavaThrow {
    Object* ex = nullptr;
};

// ---------------------------------------------------------------------------
// Vm
// ---------------------------------------------------------------------------
class Vm {
public:
    Vm(std::vector<const DexLoader::DexFile*> dexFiles, bool debug);
    ~Vm();
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;

    static Vm& get();
    static Vm* tryGet();

    const std::vector<const DexLoader::DexFile*>& dexFiles() const { return dexFiles_; }
    bool debug() const { return debug_; }
    void setDebug(bool d) { debug_ = d; }

    // ---- classes ---------------------------------------------------------
    RuntimeClass* findClass(const std::string& descriptor);      // never null
    RuntimeClass* findDexClass(const std::string& descriptor);   // null if not in any DEX
    RuntimeClass* classByType(uint32_t dexIndex, uint32_t typeIdx);
    void ensureInitialized(RuntimeClass* c);
    bool isAssignable(const RuntimeClass* from, const RuntimeClass* to) const;
    bool isInstance(Object* o, const RuntimeClass* c) const;
    RuntimeClass* classOf(Object* o);
    int dexIndexOf(const DexLoader::DexFile* dex) const;

    // ---- objects ---------------------------------------------------------
    Object* newInstance(RuntimeClass* c);
    Object* newInstance(const std::string& descriptor) { return newInstance(findClass(descriptor)); }
    StringObject* newString(const std::string& utf8);
    StringObject* intern(const std::string& utf8);
    ArrayObject* newArray(const std::string& arrayDescriptor, int32_t length);
    ArrayObject* newPrimitiveArray(ArrayObject::ArrayType t, int32_t length);
    ArrayObject* newObjectArray(const std::string& componentDescriptor, int32_t length);
    Object* box(const std::string& boxClass, uint64_t bits);
    Object* classObjectFor(RuntimeClass* c) { return c; }

    // ---- exceptions ------------------------------------------------------
    Object* makeThrowable(const std::string& descriptor, const std::string& message);
    [[noreturn]] void throwNew(const std::string& descriptor, const std::string& message = std::string());
    [[noreturn]] void throwNpe(const std::string& what = std::string());
    [[noreturn]] void throwObject(Object* ex) { throw JavaThrow{ex}; }
    std::vector<std::string> captureStack() const;
    std::string describeThrowable(Object* ex);

    // ---- invocation ------------------------------------------------------
    Value invoke(const RMethod* m, Object* self, const std::vector<Value>& args);
    Value callVirtual(Object* self, const std::string& name, const std::string& proto,
                      const std::vector<Value>& args = {});
    Value callStatic(const std::string& cls, const std::string& name,
                     const std::string& proto, const std::vector<Value>& args = {});
    const RMethod* findMethod(RuntimeClass* c, const std::string& name, const std::string& proto);
    Target resolveVirtual(RuntimeClass* start, const std::string& key,
                          const std::string& name, const std::string& proto);
    Value invokeTarget(const Target& t, const MethodRef* ref, Object* self,
                       const std::vector<Value>& args);

    // Runs `fn` as a top-level entry (holds the interpreter lock, reports an
    // uncaught Java exception instead of propagating it). Returns false when
    // a Java exception escaped.
    bool runGuarded(const std::function<void()>& fn, const std::string& what);

    // ---- Java helpers used by natives ------------------------------------
    bool javaEquals(Object* a, Object* b);
    int32_t javaHashCode(Object* o);
    std::string javaToString(Object* o);
    std::string stringOf(Object* o);               // "" for null/non-string
    void monitorEnter(Object* o);
    void monitorExit(Object* o);
    void monitorWait(Object* o, int64_t timeoutMs);   // Object.wait semantics
    void monitorNotify(Object* o, bool all);

    // ---- interpreter lock (GIL) ------------------------------------------
    void lock();
    void unlock();
    void yieldLock();
    bool ownsLock() const;
    int releaseAll();
    void reacquire(int depth);
    // Releases the lock for the duration of a blocking native call.
    struct Unlocker {
        Vm& vm;
        int saved;
        explicit Unlocker(Vm& v) : vm(v), saved(v.releaseAll()) {}
        ~Unlocker() { vm.reacquire(saved); }
    };
    struct Locker {
        Vm& vm;
        explicit Locker(Vm& v) : vm(v) { vm.lock(); }
        ~Locker() { vm.unlock(); }
    };

    // Starts a Java thread running `fn` with a large native stack.
    void spawnThread(std::function<void()> fn);

    // ---- internals shared with the interpreter loop ------------------------
    struct DexCtx {
        const DexLoader::DexFile* dex = nullptr;
        uint32_t index = 0;
        std::vector<RuntimeClass*> types;
        std::vector<StringObject*> strings;
        std::vector<std::unique_ptr<MethodRef>> methods;
        std::vector<std::unique_ptr<FieldRef>> fields;
    };
    DexCtx& ctx(uint32_t dexIndex) { return dexCtx_[dexIndex]; }
    MethodRef& methodRef(uint32_t dexIndex, uint32_t idx);
    FieldRef& fieldRef(uint32_t dexIndex, uint32_t idx);
    void resolveField(FieldRef& fr);
    Object::FieldSlot& nativeStaticSlot(FieldRef& fr);
    ThrowableData* throwableData(Object* o);
    int32_t identityHash(Object* o);
    Object* internedString(uint32_t dexIndex, uint32_t idx);

    // Thread-local interpreter state.
    struct ThreadState {
        Frame* top = nullptr;
        Object* pending = nullptr;      // exception being delivered to move-exception
        Object* threadObject = nullptr;
        uint32_t tick = 0;
        int lockDepth = 0;
        int nativeDepth = 0;
    };
    static ThreadState& ts();

    Value execute(Frame& frame);
    Value executeMethod(const RMethod* m, const Reg* args, size_t nargs, Frame* caller);
    Value dispatchInvoke(Frame& f, uint8_t op, uint32_t methodIdx, const uint16_t* regList,
                         uint32_t argc);
    bool handleException(Frame& f, uint32_t throwPc, Object* ex);

    RuntimeClass* clsObject = nullptr;
    RuntimeClass* clsString = nullptr;
    RuntimeClass* clsClass = nullptr;

    // Logging
    void log(const std::string& s) const;

    // ---- process-wide configuration / services -----------------------------
    // Free-form properties ("package", "launcher", ...) set by the launcher.
    std::unordered_map<std::string, std::string> props;

    // Main-thread message queue. By default tasks are queued here and run by
    // pumpQueue(); the launcher may install its own poster (e.g. the Win32
    // Looper) with setPoster().
    using PostFn = std::function<void(std::function<void()>, int64_t delayMs)>;
    void setPoster(PostFn f) { poster_ = std::move(f); }
    void post(std::function<void()> fn, int64_t delayMs = 0);
    // Runs every due task of the default queue. Returns the number executed.
    size_t pumpQueue(size_t maxTasks = 100000);
    bool hasPendingTasks();

private:
    friend class NativeCall;

    RuntimeClass* loadDexClass(uint32_t dexIndex, uint32_t classDefIdx);
    RuntimeClass* makeNativeClass(const std::string& descriptor);
    RuntimeClass* makeArrayClass(const std::string& descriptor);
    void linkInstanceLayout(RuntimeClass* c);
    void initStatics(RuntimeClass* c);
    Target lookupFrom(RuntimeClass* start, const std::string& key,
                      const std::string& name, const std::string& proto, bool skipAbstract);
    Target lookupInterfaces(RuntimeClass* c, const std::string& key,
                            const std::string& name, const std::string& proto, int depth);
    void stubMiss(const std::string& cls, const std::string& name, const std::string& proto);
    Value stubReturn(const std::string& proto);
    std::string protoString(const DexLoader::DexFile* dex, uint32_t protoIdx) const;

    struct ClassLoc { uint32_t dex; uint32_t def; };

    std::vector<const DexLoader::DexFile*> dexFiles_;
    std::vector<DexCtx> dexCtx_;
    std::unordered_map<std::string_view, ClassLoc> classDefs_;
    std::unordered_map<std::string, RuntimeClass*> classes_;
    std::unordered_map<std::string, StringObject*> interned_;
    bool debug_ = false;

    // interpreter lock
    std::mutex gil_;
    std::atomic<int> gilWaiters_{0};
    std::atomic<std::thread::id> gilOwner_{};

    struct Monitor {
        std::thread::id owner;
        int count = 0;
        int waiters = 0;
        int tokens = 0;
    };
    std::unordered_map<Object*, Monitor> monitors_;

    PostFn poster_;
    struct Task { int64_t dueMs; uint64_t seq; std::function<void()> fn; };
    std::mutex queueMutex_;
    std::vector<Task> queue_;
    uint64_t taskSeq_ = 0;
};

// Remaining native stack in bytes for the calling thread (best effort).
size_t stackRemaining();

} // namespace NativeDex::Runtime::BytecodeInterpreter
