// Framework-level natives that do not need a UI: enums, references, field
// updaters and the non-visual part of the Android API (Context, Activity
// lifecycle, Intent, Bundle, Handler, Log, SharedPreferences, ...).

#include "NativeHelpers.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace NativeDex::Runtime::BytecodeInterpreter {

namespace {
NativeRegistry& REG() { return NativeRegistry::instance(); }
void A(const char* cls, const char* name, const char* proto, NativeFn fn) { REG().add(cls, name, proto, std::move(fn)); }
void M(const std::vector<std::string>& cls, const char* name, const char* proto, NativeFn fn) { REG().addMulti(cls, name, proto, std::move(fn)); }
void decl(const char* cls, const char* super, std::vector<std::string> ifs = {}) { REG().declareClass(cls, super, ifs, false); }
void declI(const char* cls, std::vector<std::string> ifs = {}) { REG().declareClass(cls, "Ljava/lang/Object;", ifs, true); }

EnumData* enumOf(Vm& vm, Object* o) {
    if (!o) vm.throwNpe("enum");
    if (!o->native) o->native = std::make_unique<EnumData>();
    return o->nativeAs<EnumData>();
}

struct UpdaterData : ObjectModel::Object::NativeData {
    const RField* field = nullptr;
};

Object::FieldSlot& updSlot(NC& c, Object* target) {
    auto* d = c.self->nativeAs<UpdaterData>();
    if (!target) c.vm.throwNpe("updater target");
    if (!d || !d->field) c.vm.throwNew("Ljava/lang/RuntimeException;", "bad field updater");
    if (d->field->slot >= target->fieldSlots.size()) target->fieldSlots.resize(d->field->slot + 1);
    return target->fieldSlots[d->field->slot];
}

void makeUpdater(NC& c, const char* cls, size_t classArg, size_t nameArg) {
    Object* u = c.vm.newInstance(cls);
    auto* d = new UpdaterData();
    u->native.reset(d);
    auto* k = dynamic_cast<RuntimeClass*>(c.o(classArg));
    std::string name = c.str(nameArg);
    for (RuntimeClass* t = k; t && !d->field; t = t->parent) {
        for (const auto& f : t->fields) if (!f.isStatic && f.name == name) { d->field = &f; break; }
    }
    if (!d->field) c.vm.throwNew("Ljava/lang/RuntimeException;", "field not found: " + name);
    c.retObj(u);
}

// ---- Android message plumbing -------------------------------------------------------
struct HandlerData : ObjectModel::Object::NativeData {
    Object* callback = nullptr;
};

struct Pending {
    Object* handler = nullptr;
    Object* runnable = nullptr;
    Object* message = nullptr;
    int what = 0;
    bool cancelled = false;
};
std::mutex g_pendingMutex;
std::vector<std::shared_ptr<Pending>> g_pending;

void schedule(Vm& vm, Object* handler, Object* runnable, Object* message, int64_t delay) {
    auto p = std::make_shared<Pending>();
    p->handler = handler; p->runnable = runnable; p->message = message;
    if (message) {
        p->what = static_cast<int>(message->getIntField(0x57A7));
    }
    {
        std::lock_guard<std::mutex> lk(g_pendingMutex);
        g_pending.push_back(p);
    }
    Vm* v = &vm;
    vm.post([v, p]() {
        if (p->cancelled) return;
        {
            std::lock_guard<std::mutex> lk(g_pendingMutex);
            g_pending.erase(std::remove(g_pending.begin(), g_pending.end(), p), g_pending.end());
        }
        if (p->runnable) {
            v->callVirtual(p->runnable, "run", "()V");
        } else if (p->message) {
            auto* hd = p->handler->nativeAs<HandlerData>();
            if (hd && hd->callback &&
                v->callVirtual(hd->callback, "handleMessage", "(Landroid/os/Message;)Z", {Value::fromRef(p->message)}).i32()) return;
            v->callVirtual(p->handler, "handleMessage", "(Landroid/os/Message;)V", {Value::fromRef(p->message)});
        }
    }, delay);
}

void cancelPending(Object* handler, Object* runnable, int what, bool byWhat, bool all) {
    std::lock_guard<std::mutex> lk(g_pendingMutex);
    for (auto& p : g_pending) {
        if (p->handler != handler) continue;
        if (all || (!byWhat && runnable && p->runnable == runnable) || (byWhat && p->message && p->what == what)) p->cancelled = true;
    }
}

// SharedPreferences backed by MapData
Object* g_mainLooper = nullptr;
Object* g_mainThread = nullptr;
Object* curThread(Vm& vm) { return vm.callStatic("Ljava/lang/Thread;", "currentThread", "()Ljava/lang/Thread;").ref; }
Object* g_appContext = nullptr;
std::unordered_map<std::string, Object*> g_prefs;

struct BundleStore { };

} // namespace

// Public hook so the launcher can register the Application/Activity context.
Object* frameworkAppContext() { return g_appContext; }
void frameworkSetAppContext(Object* ctx) { g_appContext = ctx; }
// Declares the calling thread as the Android main (UI) thread.
void frameworkMarkMainThread(Vm& vm) { g_mainThread = curThread(vm); }

void registerFrameworkNatives() {
    // ThreadPoolExecutor constructors (all arities are no-ops).
    for (const char* p : {"(IIJLjava/util/concurrent/TimeUnit;Ljava/util/concurrent/BlockingQueue;)V",
                          "(IIJLjava/util/concurrent/TimeUnit;Ljava/util/concurrent/BlockingQueue;Ljava/util/concurrent/ThreadFactory;)V",
                          "(IIJLjava/util/concurrent/TimeUnit;Ljava/util/concurrent/BlockingQueue;Ljava/util/concurrent/RejectedExecutionHandler;)V",
                          "(IIJLjava/util/concurrent/TimeUnit;Ljava/util/concurrent/BlockingQueue;Ljava/util/concurrent/ThreadFactory;Ljava/util/concurrent/RejectedExecutionHandler;)V"}) {
        A("Ljava/util/concurrent/ThreadPoolExecutor;", "<init>", p, [](NC&) {});
    }
    A("Ljava/util/concurrent/ThreadPoolExecutor;", "allowCoreThreadTimeOut", "(Z)V", [](NC&) {});
    A("Ljava/util/concurrent/ThreadPoolExecutor;", "prestartAllCoreThreads", "()I", [](NC& c) { c.retInt(0); });
    A("Ljava/util/concurrent/ThreadPoolExecutor;", "getPoolSize", "()I", [](NC& c) { c.retInt(0); });
    A("Ljava/lang/String;", "lastIndexOf", "(II)I", [](NC& c) {
        auto* s = dynamic_cast<StringObject*>(c.self); if (!s) c.vm.throwNpe("lastIndexOf");
        const auto& u = s->u16(); int32_t from = std::min<int32_t>(c.i(1), static_cast<int32_t>(u.size()) - 1);
        for (int32_t i = from; i >= 0; --i) if (u[i] == static_cast<char16_t>(c.i(0))) { c.retInt(i); return; }
        c.retInt(-1);
    });
    // ======================= java.lang.Enum ============================================
    decl("Ljava/lang/Enum;", "Ljava/lang/Object;", {"Ljava/lang/Comparable;", "Ljava/io/Serializable;"});
    A("Ljava/lang/Enum;", "<init>", "(Ljava/lang/String;I)V", [](NC& c) {
        auto* d = enumOf(c.vm, c.self); d->name = c.str(0); d->ordinal = c.i(1);
    });
    A("Ljava/lang/Enum;", "name", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(enumOf(c.vm, c.self)->name)); });
    A("Ljava/lang/Enum;", "toString", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(enumOf(c.vm, c.self)->name)); });
    A("Ljava/lang/Enum;", "ordinal", "()I", [](NC& c) { c.retInt(enumOf(c.vm, c.self)->ordinal); });
    A("Ljava/lang/Enum;", "compareTo", "(Ljava/lang/Enum;)I", [](NC& c) { c.retInt(enumOf(c.vm, c.self)->ordinal - enumOf(c.vm, c.o(0))->ordinal); });
    A("Ljava/lang/Enum;", "compareTo", "(Ljava/lang/Object;)I", [](NC& c) { c.retInt(enumOf(c.vm, c.self)->ordinal - enumOf(c.vm, c.o(0))->ordinal); });
    A("Ljava/lang/Enum;", "equals", "(Ljava/lang/Object;)Z", [](NC& c) { c.retBool(c.self == c.o(0)); });
    A("Ljava/lang/Enum;", "hashCode", "()I", [](NC& c) { c.retInt(c.vm.identityHash(c.self)); });
    A("Ljava/lang/Enum;", "getDeclaringClass", "()Ljava/lang/Class;", [](NC& c) {
        RuntimeClass* k = c.vm.classOf(c.self);
        while (k && k->parent && k->parent->getDescriptor() != "Ljava/lang/Enum;") k = k->parent;
        c.retObj(k);
    });
    A("Ljava/lang/Enum;", "valueOf", "(Ljava/lang/Class;Ljava/lang/String;)Ljava/lang/Enum;", [](NC& c) {
        auto* k = static_cast<RuntimeClass*>(c.o(0));
        std::string want = c.str(1);
        c.vm.ensureInitialized(k);
        for (const auto& f : k->fields) {
            if (f.isStatic && f.name == want && f.type == k->getDescriptor() && f.slot < k->statics.size()) { c.retObj(k->statics[f.slot].ref); return; }
        }
        c.vm.throwNew("Ljava/lang/IllegalArgumentException;", "No enum constant " + k->getClassName() + "." + want);
    });

    decl("Ljava/util/concurrent/TimeUnit;", "Ljava/lang/Enum;");
    REG().setEnumOrder("Ljava/util/concurrent/TimeUnit;", {"NANOSECONDS", "MICROSECONDS", "MILLISECONDS", "SECONDS", "MINUTES", "HOURS", "DAYS"});
    static const double NS[] = {1.0, 1e3, 1e6, 1e9, 6e10, 3.6e12, 8.64e13};
    auto toUnit = [](int target) {
        return [target](NC& c) {
            int self = enumOf(c.vm, c.self)->ordinal;
            double v = static_cast<double>(c.j(0)) * NS[self] / NS[target];
            c.retLong(v >= 9.2e18 ? INT64_MAX : (v <= -9.2e18 ? INT64_MIN : static_cast<int64_t>(v)));
        };
    };
    A("Ljava/util/concurrent/TimeUnit;", "toNanos", "(J)J", toUnit(0));
    A("Ljava/util/concurrent/TimeUnit;", "toMicros", "(J)J", toUnit(1));
    A("Ljava/util/concurrent/TimeUnit;", "toMillis", "(J)J", toUnit(2));
    A("Ljava/util/concurrent/TimeUnit;", "toSeconds", "(J)J", toUnit(3));
    A("Ljava/util/concurrent/TimeUnit;", "toMinutes", "(J)J", toUnit(4));
    A("Ljava/util/concurrent/TimeUnit;", "toHours", "(J)J", toUnit(5));
    A("Ljava/util/concurrent/TimeUnit;", "toDays", "(J)J", toUnit(6));
    A("Ljava/util/concurrent/TimeUnit;", "convert", "(JLjava/util/concurrent/TimeUnit;)J", [](NC& c) {
        int self = enumOf(c.vm, c.self)->ordinal, src = enumOf(c.vm, c.o(1))->ordinal;
        c.retLong(static_cast<int64_t>(static_cast<double>(c.j(0)) * NS[src] / NS[self]));
    });
    A("Ljava/util/concurrent/TimeUnit;", "sleep", "(J)V", [](NC& c) {
        int self = enumOf(c.vm, c.self)->ordinal;
        int64_t ms = static_cast<int64_t>(static_cast<double>(c.j(0)) * NS[self] / 1e6);
        Vm::Unlocker u(c.vm); std::this_thread::sleep_for(std::chrono::milliseconds(std::max<int64_t>(0, ms)));
    });

    // ======================= references ================================================
    decl("Ljava/lang/ref/Reference;", "Ljava/lang/Object;");
    for (const char* k : {"Ljava/lang/ref/WeakReference;", "Ljava/lang/ref/SoftReference;", "Ljava/lang/ref/PhantomReference;"}) decl(k, "Ljava/lang/ref/Reference;");
    auto refOf = [](NC& c) -> RefData* { if (!c.self->native) c.self->native = std::make_unique<RefData>(); return c.self->nativeAs<RefData>(); };
    M({"Ljava/lang/ref/WeakReference;", "Ljava/lang/ref/SoftReference;", "Ljava/lang/ref/PhantomReference;"}, "<init>", "(Ljava/lang/Object;)V", [refOf](NC& c) { refOf(c)->v = c.o(0); });
    M({"Ljava/lang/ref/WeakReference;", "Ljava/lang/ref/SoftReference;", "Ljava/lang/ref/PhantomReference;"}, "<init>", "(Ljava/lang/Object;Ljava/lang/ref/ReferenceQueue;)V", [refOf](NC& c) { refOf(c)->v = c.o(0); });
    A("Ljava/lang/ref/Reference;", "get", "()Ljava/lang/Object;", [refOf](NC& c) { c.retObj(refOf(c)->v); });
    A("Ljava/lang/ref/Reference;", "clear", "()V", [refOf](NC& c) { refOf(c)->v = nullptr; });
    A("Ljava/lang/ref/Reference;", "enqueue", "()Z", [](NC& c) { c.retBool(false); });
    A("Ljava/lang/ref/ReferenceQueue;", "<init>", "()V", [](NC&) {});
    A("Ljava/lang/ref/ReferenceQueue;", "poll", "()Ljava/lang/ref/Reference;", [](NC& c) { c.retObj(nullptr); });
    A("Ljava/lang/ThreadLocal;", "<init>", "()V", [](NC&) {});
    A("Ljava/lang/ThreadLocal;", "get", "()Ljava/lang/Object;", [](NC& c) {
        auto it = c.self->legacy ? c.self->legacy->objFields.find(reinterpret_cast<uint64_t>(&Vm::ts())) : decltype(c.self->legacy->objFields.end()){};
        Object* v = (c.self->legacy && it != c.self->legacy->objFields.end()) ? it->second : nullptr;
        if (!v && c.self->klass) {
            Target t = c.vm.resolveVirtual(c.vm.classOf(c.self), "initialValue()Ljava/lang/Object;", "initialValue", "()Ljava/lang/Object;");
            if (t.valid() && t.method) { v = c.vm.invokeTarget(t, nullptr, c.self, {}).ref; c.self->setObjField(reinterpret_cast<uint64_t>(&Vm::ts()), v); }
        }
        c.retObj(v);
    });
    A("Ljava/lang/ThreadLocal;", "set", "(Ljava/lang/Object;)V", [](NC& c) { c.self->setObjField(reinterpret_cast<uint64_t>(&Vm::ts()), c.o(0)); });
    A("Ljava/lang/ThreadLocal;", "remove", "()V", [](NC& c) { c.self->setObjField(reinterpret_cast<uint64_t>(&Vm::ts()), nullptr); });

    // ======================= atomic field updaters ====================================
    const char* ARU = "Ljava/util/concurrent/atomic/AtomicReferenceFieldUpdater;";
    const char* AIU = "Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;";
    const char* ALU = "Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;";
    A(ARU, "newUpdater", "(Ljava/lang/Class;Ljava/lang/Class;Ljava/lang/String;)Ljava/util/concurrent/atomic/AtomicReferenceFieldUpdater;", [ARU](NC& c) { makeUpdater(c, ARU, 0, 2); });
    A(AIU, "newUpdater", "(Ljava/lang/Class;Ljava/lang/String;)Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", [AIU](NC& c) { makeUpdater(c, AIU, 0, 1); });
    A(ALU, "newUpdater", "(Ljava/lang/Class;Ljava/lang/String;)Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", [ALU](NC& c) { makeUpdater(c, ALU, 0, 1); });
    A(ARU, "compareAndSet", "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)Z", [](NC& c) { auto& s = updSlot(c, c.o(0)); if (s.ref == c.o(1)) { s.ref = c.o(2); c.retBool(true); } else c.retBool(false); });
    A(ARU, "weakCompareAndSet", "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)Z", [](NC& c) { auto& s = updSlot(c, c.o(0)); if (s.ref == c.o(1)) { s.ref = c.o(2); c.retBool(true); } else c.retBool(false); });
    A(ARU, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) { c.retObj(updSlot(c, c.o(0)).ref); });
    A(ARU, "set", "(Ljava/lang/Object;Ljava/lang/Object;)V", [](NC& c) { updSlot(c, c.o(0)).ref = c.o(1); });
    A(ARU, "lazySet", "(Ljava/lang/Object;Ljava/lang/Object;)V", [](NC& c) { updSlot(c, c.o(0)).ref = c.o(1); });
    A(ARU, "getAndSet", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) { auto& s = updSlot(c, c.o(0)); Object* o = s.ref; s.ref = c.o(1); c.retObj(o); });
    A(AIU, "compareAndSet", "(Ljava/lang/Object;II)Z", [](NC& c) { auto& s = updSlot(c, c.o(0)); if (static_cast<int32_t>(s.bits) == c.i(1)) { s.bits = static_cast<uint32_t>(c.i(2)); c.retBool(true); } else c.retBool(false); });
    A(AIU, "get", "(Ljava/lang/Object;)I", [](NC& c) { c.retInt(static_cast<int32_t>(updSlot(c, c.o(0)).bits)); });
    A(AIU, "set", "(Ljava/lang/Object;I)V", [](NC& c) { updSlot(c, c.o(0)).bits = static_cast<uint32_t>(c.i(1)); });
    A(AIU, "incrementAndGet", "(Ljava/lang/Object;)I", [](NC& c) { auto& s = updSlot(c, c.o(0)); s.bits = static_cast<uint32_t>(static_cast<int32_t>(s.bits) + 1); c.retInt(static_cast<int32_t>(s.bits)); });
    A(AIU, "getAndIncrement", "(Ljava/lang/Object;)I", [](NC& c) { auto& s = updSlot(c, c.o(0)); int32_t o = static_cast<int32_t>(s.bits); s.bits = static_cast<uint32_t>(o + 1); c.retInt(o); });
    A(AIU, "addAndGet", "(Ljava/lang/Object;I)I", [](NC& c) { auto& s = updSlot(c, c.o(0)); s.bits = static_cast<uint32_t>(static_cast<int32_t>(s.bits) + c.i(1)); c.retInt(static_cast<int32_t>(s.bits)); });
    A(ALU, "compareAndSet", "(Ljava/lang/Object;JJ)Z", [](NC& c) { auto& s = updSlot(c, c.o(0)); if (static_cast<int64_t>(s.bits) == c.j(1)) { s.bits = static_cast<uint64_t>(c.j(2)); c.retBool(true); } else c.retBool(false); });
    A(ALU, "get", "(Ljava/lang/Object;)J", [](NC& c) { c.retLong(static_cast<int64_t>(updSlot(c, c.o(0)).bits)); });
    A(ALU, "set", "(Ljava/lang/Object;J)V", [](NC& c) { updSlot(c, c.o(0)).bits = static_cast<uint64_t>(c.j(1)); });
    A(ALU, "addAndGet", "(Ljava/lang/Object;J)J", [](NC& c) { auto& s = updSlot(c, c.o(0)); s.bits += static_cast<uint64_t>(c.j(1)); c.retLong(static_cast<int64_t>(s.bits)); });
    A(ALU, "incrementAndGet", "(Ljava/lang/Object;)J", [](NC& c) { auto& s = updSlot(c, c.o(0)); s.bits += 1; c.retLong(static_cast<int64_t>(s.bits)); });

    // ======================= Android (non-visual) ======================================
    decl("Landroid/content/Context;", "Ljava/lang/Object;");
    decl("Landroid/content/ContextWrapper;", "Landroid/content/Context;");
    decl("Landroid/view/ContextThemeWrapper;", "Landroid/content/ContextWrapper;");
    decl("Landroid/app/Application;", "Landroid/content/ContextWrapper;");
    decl("Landroid/app/Service;", "Landroid/content/ContextWrapper;");
    decl("Landroid/app/Activity;", "Landroid/view/ContextThemeWrapper;");
    decl("Landroid/os/Handler;", "Ljava/lang/Object;");
    decl("Landroid/os/Looper;", "Ljava/lang/Object;");
    decl("Landroid/os/Message;", "Ljava/lang/Object;");
    decl("Landroid/os/Bundle;", "Ljava/lang/Object;");
    decl("Landroid/content/Intent;", "Ljava/lang/Object;");
    decl("Landroid/content/SharedPreferences$Editor;", "Ljava/lang/Object;");
    decl("Landroid/content/SharedPreferencesImpl;", "Ljava/lang/Object;", {"Landroid/content/SharedPreferences;"});
    declI("Landroid/content/SharedPreferences;");
    declI("Landroid/os/Parcelable;");
    REG().setStaticInt("Landroid/os/Build$VERSION;", "SDK_INT", 33);
    REG().setStaticInt("Landroid/os/Build$VERSION;", "PREVIEW_SDK_INT", 0);
    REG().setStaticString("Landroid/os/Build$VERSION;", "RELEASE", "13");
    REG().setStaticString("Landroid/os/Build;", "MODEL", "NativeDex");
    REG().setStaticString("Landroid/os/Build;", "MANUFACTURER", "NativeDex");
    REG().setStaticString("Landroid/os/Build;", "BRAND", "NativeDex");
    REG().setStaticString("Landroid/os/Build;", "DEVICE", "nativedex");
    REG().setStaticString("Landroid/os/Build;", "PRODUCT", "nativedex");
    REG().setStaticString("Landroid/os/Build;", "FINGERPRINT", "nativedex/nativedex:13/NDX/0:user/release-keys");
    REG().setStaticString("Landroid/os/Build;", "HARDWARE", "nativedex");
    REG().setStaticString("Landroid/os/Build;", "BOARD", "nativedex");
    REG().setStaticString("Landroid/os/Build;", "ID", "NDX");
    REG().setStaticString("Landroid/os/Build;", "TAGS", "release-keys");
    REG().setStaticString("Landroid/os/Build;", "TYPE", "user");
    REG().setStaticString("Landroid/os/Build;", "DISPLAY", "NDX");
    REG().setStaticString("Landroid/os/Build;", "HOST", "nativedex");
    REG().setStaticString("Landroid/os/Build;", "USER", "nativedex");
    REG().setStaticInt("Landroid/content/Context;", "MODE_PRIVATE", 0);
    REG().setStaticInt("Landroid/app/Activity;", "RESULT_OK", -1);
    REG().setStaticInt("Landroid/app/Activity;", "RESULT_CANCELED", 0);

    // ---- android.util.Log -------------------------------------------------------------
    for (const char* lvl : {"v", "d", "i", "w", "e", "wtf"}) {
        std::string L = lvl;
        A("Landroid/util/Log;", lvl, "(Ljava/lang/String;Ljava/lang/String;)I", [L](NC& c) {
            if (c.vm.debug()) std::cerr << "[Log." << L << "] " << c.str(0) << ": " << c.str(1) << std::endl;
            c.retInt(0);
        });
        A("Landroid/util/Log;", lvl, "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", [L](NC& c) {
            if (c.vm.debug()) std::cerr << "[Log." << L << "] " << c.str(0) << ": " << c.str(1) << std::endl;
            c.retInt(0);
        });
    }
    A("Landroid/util/Log;", "isLoggable", "(Ljava/lang/String;I)Z", [](NC& c) { c.retBool(false); });
    A("Landroid/util/Log;", "getStackTraceString", "(Ljava/lang/Throwable;)Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(c.o(0) ? c.vm.describeThrowable(c.o(0)) : "")); });

    // ---- android.os.SystemClock -----------------------------------------------------------
    auto up = [](NC& c) { c.retLong(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); };
    A("Landroid/os/SystemClock;", "uptimeMillis", "()J", up);
    A("Landroid/os/SystemClock;", "elapsedRealtime", "()J", up);
    A("Landroid/os/SystemClock;", "currentThreadTimeMillis", "()J", up);
    A("Landroid/os/SystemClock;", "elapsedRealtimeNanos", "()J", [](NC& c) { c.retLong(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); });
    A("Landroid/os/SystemClock;", "sleep", "(J)V", [](NC& c) { Vm::Unlocker u(c.vm); std::this_thread::sleep_for(std::chrono::milliseconds(std::max<int64_t>(0, c.j(0)))); });

    // ---- Looper / Handler / Message -------------------------------------------------------------
    auto mainLooper = [](NC& c) -> Object* {
        if (!g_mainLooper) g_mainLooper = c.vm.newInstance("Landroid/os/Looper;");
        return g_mainLooper;
    };
    A("Landroid/os/Looper;", "getMainLooper", "()Landroid/os/Looper;", [mainLooper](NC& c) { c.retObj(mainLooper(c)); });
    A("Landroid/os/Looper;", "myLooper", "()Landroid/os/Looper;", [mainLooper](NC& c) {
        Object* t = curThread(c.vm);
        if (!g_mainThread) g_mainThread = t;
        c.retObj(t == g_mainThread ? mainLooper(c) : nullptr);
    });
    A("Landroid/os/Looper;", "prepare", "()V", [](NC&) {});
    A("Landroid/os/Looper;", "prepareMainLooper", "()V", [](NC&) {});
    A("Landroid/os/Looper;", "loop", "()V", [](NC&) {});
    A("Landroid/os/Looper;", "quit", "()V", [](NC&) {});
    A("Landroid/os/Looper;", "quitSafely", "()V", [](NC&) {});
    A("Landroid/os/Looper;", "getThread", "()Ljava/lang/Thread;", [](NC& c) {
        if (!g_mainThread) g_mainThread = curThread(c.vm);
        c.retObj(g_mainThread);
    });
    A("Landroid/os/Looper;", "isCurrentThread", "()Z", [](NC& c) {
        Object* t = curThread(c.vm);
        if (!g_mainThread) g_mainThread = t;
        c.retBool(t == g_mainThread);
    });

    auto hd = [](NC& c) -> HandlerData* { if (!c.self->native) c.self->native = std::make_unique<HandlerData>(); return c.self->nativeAs<HandlerData>(); };
    A("Landroid/os/Handler;", "<init>", "()V", [hd](NC& c) { hd(c); });
    A("Landroid/os/Handler;", "<init>", "(Landroid/os/Looper;)V", [hd](NC& c) { hd(c); });
    A("Landroid/os/Handler;", "<init>", "(Landroid/os/Handler$Callback;)V", [hd](NC& c) { hd(c)->callback = c.o(0); });
    A("Landroid/os/Handler;", "<init>", "(Landroid/os/Looper;Landroid/os/Handler$Callback;)V", [hd](NC& c) { hd(c)->callback = c.o(1); });
    A("Landroid/os/Handler;", "getLooper", "()Landroid/os/Looper;", [mainLooper](NC& c) { c.retObj(mainLooper(c)); });
    A("Landroid/os/Handler;", "post", "(Ljava/lang/Runnable;)Z", [](NC& c) { schedule(c.vm, c.self, c.o(0), nullptr, 0); c.retBool(true); });
    A("Landroid/os/Handler;", "postAtFrontOfQueue", "(Ljava/lang/Runnable;)Z", [](NC& c) { schedule(c.vm, c.self, c.o(0), nullptr, 0); c.retBool(true); });
    A("Landroid/os/Handler;", "postDelayed", "(Ljava/lang/Runnable;J)Z", [](NC& c) { schedule(c.vm, c.self, c.o(0), nullptr, c.j(1)); c.retBool(true); });
    A("Landroid/os/Handler;", "postDelayed", "(Ljava/lang/Runnable;Ljava/lang/Object;J)Z", [](NC& c) { schedule(c.vm, c.self, c.o(0), nullptr, c.j(2)); c.retBool(true); });
    A("Landroid/os/Handler;", "postAtTime", "(Ljava/lang/Runnable;J)Z", [](NC& c) { schedule(c.vm, c.self, c.o(0), nullptr, 0); c.retBool(true); });
    A("Landroid/os/Handler;", "sendMessage", "(Landroid/os/Message;)Z", [](NC& c) { schedule(c.vm, c.self, nullptr, c.o(0), 0); c.retBool(true); });
    A("Landroid/os/Handler;", "sendMessageDelayed", "(Landroid/os/Message;J)Z", [](NC& c) { schedule(c.vm, c.self, nullptr, c.o(0), c.j(1)); c.retBool(true); });
    A("Landroid/os/Handler;", "sendMessageAtTime", "(Landroid/os/Message;J)Z", [](NC& c) { schedule(c.vm, c.self, nullptr, c.o(0), 0); c.retBool(true); });
    A("Landroid/os/Handler;", "sendEmptyMessage", "(I)Z", [](NC& c) {
        Object* m = c.vm.newInstance("Landroid/os/Message;"); m->setIntField(0x57A7, static_cast<uint32_t>(c.i(0)));
        schedule(c.vm, c.self, nullptr, m, 0); c.retBool(true);
    });
    A("Landroid/os/Handler;", "sendEmptyMessageDelayed", "(IJ)Z", [](NC& c) {
        Object* m = c.vm.newInstance("Landroid/os/Message;"); m->setIntField(0x57A7, static_cast<uint32_t>(c.i(0)));
        schedule(c.vm, c.self, nullptr, m, c.j(1)); c.retBool(true);
    });
    A("Landroid/os/Handler;", "obtainMessage", "()Landroid/os/Message;", [](NC& c) { c.retObj(c.vm.newInstance("Landroid/os/Message;")); });
    A("Landroid/os/Handler;", "obtainMessage", "(I)Landroid/os/Message;", [](NC& c) { Object* m = c.vm.newInstance("Landroid/os/Message;"); m->setIntField(0x57A7, static_cast<uint32_t>(c.i(0))); c.retObj(m); });
    A("Landroid/os/Handler;", "removeCallbacks", "(Ljava/lang/Runnable;)V", [](NC& c) { cancelPending(c.self, c.o(0), 0, false, false); });
    A("Landroid/os/Handler;", "removeMessages", "(I)V", [](NC& c) { cancelPending(c.self, nullptr, c.i(0), true, false); });
    A("Landroid/os/Handler;", "removeCallbacksAndMessages", "(Ljava/lang/Object;)V", [](NC& c) { cancelPending(c.self, nullptr, 0, false, true); });
    A("Landroid/os/Handler;", "hasMessages", "(I)Z", [](NC& c) { c.retBool(false); });
    A("Landroid/os/Handler;", "dispatchMessage", "(Landroid/os/Message;)V", [](NC& c) { c.vm.callVirtual(c.self, "handleMessage", "(Landroid/os/Message;)V", {Value::fromRef(c.o(0))}); });
    A("Landroid/os/Handler;", "handleMessage", "(Landroid/os/Message;)V", [](NC&) {});
    A("Landroid/os/Message;", "obtain", "()Landroid/os/Message;", [](NC& c) { c.retObj(c.vm.newInstance("Landroid/os/Message;")); });
    A("Landroid/os/Message;", "obtain", "(Landroid/os/Handler;I)Landroid/os/Message;", [](NC& c) { Object* m = c.vm.newInstance("Landroid/os/Message;"); m->setIntField(0x57A7, static_cast<uint32_t>(c.i(1))); c.retObj(m); });
    A("Landroid/os/Message;", "obtain", "(Landroid/os/Handler;)Landroid/os/Message;", [](NC& c) { c.retObj(c.vm.newInstance("Landroid/os/Message;")); });
    A("Landroid/os/Message;", "recycle", "()V", [](NC&) {});
    A("Landroid/os/Message;", "<init>", "()V", [](NC&) {});
    A("Landroid/os/Message;", "sendToTarget", "()V", [](NC&) {});

    // ---- Bundle / Intent (string-keyed maps) -----------------------------------------------------------
    const std::vector<std::string> BI = {"Landroid/os/Bundle;", "Landroid/content/Intent;", "Landroid/os/PersistableBundle;"};
    auto store = [](NC& c) -> MapData* { return mapOf(c.vm, c.self); };
    M(BI, "<init>", "()V", [store](NC& c) { store(c); });
    A("Landroid/os/Bundle;", "<init>", "(Landroid/os/Bundle;)V", [store](NC& c) {
        auto* m = store(c);
        if (auto* src = c.o(0) ? dynamic_cast<MapData*>(c.o(0)->native.get()) : nullptr)
            for (auto& e : src->entries) if (e.live) mapPut(c.vm, m, e.k, e.v);
    });
    struct Put { const char* name; const char* proto; int kind; };
    static const Put puts[] = {
        {"putString", "(Ljava/lang/String;Ljava/lang/String;)V", 0}, {"putInt", "(Ljava/lang/String;I)V", 1},
        {"putLong", "(Ljava/lang/String;J)V", 2}, {"putBoolean", "(Ljava/lang/String;Z)V", 3},
        {"putFloat", "(Ljava/lang/String;F)V", 4}, {"putDouble", "(Ljava/lang/String;D)V", 5},
        {"putParcelable", "(Ljava/lang/String;Landroid/os/Parcelable;)V", 0}, {"putSerializable", "(Ljava/lang/String;Ljava/io/Serializable;)V", 0},
        {"putBundle", "(Ljava/lang/String;Landroid/os/Bundle;)V", 0}, {"putCharSequence", "(Ljava/lang/String;Ljava/lang/CharSequence;)V", 0},
        {"putStringArray", "(Ljava/lang/String;[Ljava/lang/String;)V", 0}, {"putIntArray", "(Ljava/lang/String;[I)V", 0},
        {"putByteArray", "(Ljava/lang/String;[B)V", 0}, {"putStringArrayList", "(Ljava/lang/String;Ljava/util/ArrayList;)V", 0},
    };
    static const char* boxes[] = {nullptr, "Ljava/lang/Integer;", "Ljava/lang/Long;", "Ljava/lang/Boolean;", "Ljava/lang/Float;", "Ljava/lang/Double;"};
    for (const auto& p : puts) {
        int kind = p.kind;
        M({"Landroid/os/Bundle;", "Landroid/os/PersistableBundle;"}, p.name, p.proto, [store, kind](NC& c) {
            Object* v = kind == 0 ? c.o(1) : c.vm.box(boxes[kind], c.args[1].bits);
            mapPut(c.vm, store(c), c.o(0), v);
        });
        std::string intentProto = p.proto;
        size_t rp = intentProto.rfind(")V");
        intentProto = intentProto.substr(0, rp) + ")Landroid/content/Intent;";
        if (std::string(p.name) != "putBundle") {
            std::string nm = std::string("putExtra");
            A("Landroid/content/Intent;", nm.c_str(), intentProto.c_str(), [store, kind](NC& c) {
                Object* v = kind == 0 ? c.o(1) : c.vm.box(boxes[kind], c.args[1].bits);
                mapPut(c.vm, store(c), c.o(0), v);
                c.retObj(c.self);
            });
        }
    }
    A("Landroid/content/Intent;", "putExtras", "(Landroid/os/Bundle;)Landroid/content/Intent;", [store](NC& c) {
        auto* m = store(c);
        if (auto* src = c.o(0) ? dynamic_cast<MapData*>(c.o(0)->native.get()) : nullptr)
            for (auto& e : src->entries) if (e.live) mapPut(c.vm, m, e.k, e.v);
        c.retObj(c.self);
    });
    struct Get { const char* name; const char* ret; int kind; };
    static const Get gets[] = {
        {"getString", "Ljava/lang/String;", 0}, {"getInt", "I", 1}, {"getLong", "J", 2}, {"getBoolean", "Z", 3},
        {"getFloat", "F", 4}, {"getDouble", "D", 5}, {"getParcelable", "Landroid/os/Parcelable;", 0},
        {"getSerializable", "Ljava/io/Serializable;", 0}, {"getBundle", "Landroid/os/Bundle;", 0},
        {"getCharSequence", "Ljava/lang/CharSequence;", 0}, {"getStringArray", "[Ljava/lang/String;", 0},
        {"getIntArray", "[I", 0}, {"getByteArray", "[B", 0}, {"getStringArrayList", "Ljava/util/ArrayList;", 0}};
    auto getter = [store](NC& c, int kind, size_t defIdx, bool hasDef) {
        bool f = false;
        Object* v = mapGet(c.vm, store(c), c.o(0), &f);
        if (kind == 0) { c.retObj(f ? v : (hasDef ? c.o(defIdx) : nullptr)); return; }
        if (!f || !v) { if (hasDef) c.ret = c.args[defIdx]; else c.ret = Value(); return; }
        Value r; r.bits = v->nativeHandle; c.ret = r;
    };
    for (const auto& g : gets) {
        int kind = g.kind;
        std::string base = std::string("(Ljava/lang/String;)") + g.ret;
        std::string def;
        char rc = g.ret[0];
        if (kind == 0) def = std::string("(Ljava/lang/String;") + g.ret + ")" + g.ret;
        else def = std::string("(Ljava/lang/String;") + g.ret + ")" + g.ret;
        M({"Landroid/os/Bundle;", "Landroid/os/PersistableBundle;"}, g.name, base.c_str(), [getter, kind](NC& c) { getter(c, kind, 1, false); });
        M({"Landroid/os/Bundle;", "Landroid/os/PersistableBundle;"}, g.name, def.c_str(), [getter, kind](NC& c) { getter(c, kind, 1, true); });
        (void)rc;
        std::string iname = std::string(g.name) + "Extra";
        if (kind != 0 || std::string(g.name) == "getString") {
            A("Landroid/content/Intent;", iname.c_str(), base.c_str(), [getter, kind](NC& c) { getter(c, kind, 1, false); });
            A("Landroid/content/Intent;", iname.c_str(), def.c_str(), [getter, kind](NC& c) { getter(c, kind, 1, true); });
        }
    }
    for (const char* ex : {"getParcelableExtra:(Ljava/lang/String;)Landroid/os/Parcelable;", "getSerializableExtra:(Ljava/lang/String;)Ljava/io/Serializable;",
                           "getStringArrayExtra:(Ljava/lang/String;)[Ljava/lang/String;", "getBundleExtra:(Ljava/lang/String;)Landroid/os/Bundle;",
                           "getCharSequenceExtra:(Ljava/lang/String;)Ljava/lang/CharSequence;", "getStringArrayListExtra:(Ljava/lang/String;)Ljava/util/ArrayList;"}) {
        std::string s = ex; size_t p = s.find(':');
        A("Landroid/content/Intent;", s.substr(0, p).c_str(), s.substr(p + 1).c_str(), [store](NC& c) { c.retObj(mapGet(c.vm, store(c), c.o(0))); });
    }
    A("Landroid/content/Intent;", "getExtras", "()Landroid/os/Bundle;", [store](NC& c) {
        Object* b = c.vm.newInstance("Landroid/os/Bundle;"); auto* m = mapOf(c.vm, b); auto* src = store(c);
        for (auto& e : src->entries) if (e.live) mapPut(c.vm, m, e.k, e.v);
        c.retObj(b);
    });
    M(BI, "containsKey", "(Ljava/lang/String;)Z", [store](NC& c) { bool f; mapGet(c.vm, store(c), c.o(0), &f); c.retBool(f); });
    A("Landroid/content/Intent;", "hasExtra", "(Ljava/lang/String;)Z", [store](NC& c) { bool f; mapGet(c.vm, store(c), c.o(0), &f); c.retBool(f); });
    M(BI, "remove", "(Ljava/lang/String;)V", [store](NC& c) { mapRemove(c.vm, store(c), c.o(0)); });
    M(BI, "isEmpty", "()Z", [store](NC& c) { c.retBool(store(c)->liveCount == 0); });
    M(BI, "size", "()I", [store](NC& c) { c.retInt(static_cast<int32_t>(store(c)->liveCount)); });
    M(BI, "clear", "()V", [store](NC& c) { mapClear(store(c)); });
    A("Landroid/os/Bundle;", "keySet", "()Ljava/util/Set;", [store](NC& c) {
        Object* s = c.vm.newInstance("Ljava/util/LinkedHashSet;"); auto* sm = mapOf(c.vm, s); sm->isSet = true;
        for (auto& e : store(c)->entries) if (e.live) mapPut(c.vm, sm, e.k, nullptr);
        c.retObj(s);
    });
    // Intent addressing
    auto intentField = [](NC& c, uint64_t key, Object* v) { c.self->setObjField(key, v); };
    A("Landroid/content/Intent;", "<init>", "(Ljava/lang/String;)V", [store, intentField](NC& c) { store(c); intentField(c, 0x1A01, c.o(0)); });
    A("Landroid/content/Intent;", "<init>", "(Landroid/content/Context;Ljava/lang/Class;)V", [store, intentField](NC& c) { store(c); intentField(c, 0x1A02, c.o(1)); });
    A("Landroid/content/Intent;", "<init>", "(Landroid/content/Intent;)V", [store](NC& c) {
        auto* m = store(c);
        if (auto* src = c.o(0) ? dynamic_cast<MapData*>(c.o(0)->native.get()) : nullptr)
            for (auto& e : src->entries) if (e.live) mapPut(c.vm, m, e.k, e.v);
        if (c.o(0)) { c.self->setObjField(0x1A01, c.o(0)->getObjField(0x1A01)); c.self->setObjField(0x1A02, c.o(0)->getObjField(0x1A02)); }
    });
    A("Landroid/content/Intent;", "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V", [store, intentField](NC& c) { store(c); intentField(c, 0x1A01, c.o(0)); });
    A("Landroid/content/Intent;", "setAction", "(Ljava/lang/String;)Landroid/content/Intent;", [intentField](NC& c) { intentField(c, 0x1A01, c.o(0)); c.retObj(c.self); });
    A("Landroid/content/Intent;", "getAction", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.self->getObjField(0x1A01)); });
    A("Landroid/content/Intent;", "setClass", "(Landroid/content/Context;Ljava/lang/Class;)Landroid/content/Intent;", [intentField](NC& c) { intentField(c, 0x1A02, c.o(1)); c.retObj(c.self); });
    A("Landroid/content/Intent;", "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;", [intentField](NC& c) {
        std::string d = "L" + c.str(1) + ";"; for (auto& ch : d) if (ch == '.') ch = '/';
        intentField(c, 0x1A02, c.vm.findClass(d)); c.retObj(c.self);
    });
    A("Landroid/content/Intent;", "setPackage", "(Ljava/lang/String;)Landroid/content/Intent;", [](NC& c) { c.retObj(c.self); });
    A("Landroid/content/Intent;", "addFlags", "(I)Landroid/content/Intent;", [](NC& c) { c.self->setIntField(0x1A03, c.self->getIntField(0x1A03) | static_cast<uint32_t>(c.i(0))); c.retObj(c.self); });
    A("Landroid/content/Intent;", "setFlags", "(I)Landroid/content/Intent;", [](NC& c) { c.self->setIntField(0x1A03, static_cast<uint32_t>(c.i(0))); c.retObj(c.self); });
    A("Landroid/content/Intent;", "getFlags", "()I", [](NC& c) { c.retInt(static_cast<int32_t>(c.self->getIntField(0x1A03))); });
    A("Landroid/content/Intent;", "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;", [](NC& c) { c.retObj(c.self); });
    A("Landroid/content/Intent;", "setType", "(Ljava/lang/String;)Landroid/content/Intent;", [](NC& c) { c.retObj(c.self); });
    A("Landroid/content/Intent;", "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", [](NC& c) { c.retObj(c.self); });
    A("Landroid/content/Intent;", "getData", "()Landroid/net/Uri;", [](NC& c) { c.retObj(nullptr); });
    A("Landroid/content/Intent;", "getComponent", "()Landroid/content/ComponentName;", [](NC& c) { c.retObj(nullptr); });

    // ---- SharedPreferences (in-memory) ----------------------------------------------------------------------
    auto prefs = [](NC& c) -> Object* {
        std::string n = c.str(0);
        auto it = g_prefs.find(n);
        if (it != g_prefs.end()) return it->second;
        Object* p = c.vm.newInstance("Landroid/content/SharedPreferencesImpl;");
        mapOf(c.vm, p);
        g_prefs[n] = p;
        return p;
    };
    const char* SP = "Landroid/content/SharedPreferences;";
    A(SP, "contains", "(Ljava/lang/String;)Z", [](NC& c) { bool f; mapGet(c.vm, mapOf(c.vm, c.self), c.o(0), &f); c.retBool(f); });
    for (const auto& g : gets) {
        if (std::string(g.name) == "getParcelable" || g.kind > 5 || std::string(g.name).find("Array") != std::string::npos || std::string(g.name) == "getBundle" || std::string(g.name) == "getSerializable" || std::string(g.name) == "getCharSequence" || std::string(g.name) == "getStringArrayList") continue;
        int kind = g.kind;
        std::string def = std::string("(Ljava/lang/String;") + g.ret + ")" + g.ret;
        A(SP, g.name, def.c_str(), [kind](NC& c) {
            bool f = false; Object* v = mapGet(c.vm, mapOf(c.vm, c.self), c.o(0), &f);
            if (kind == 0) { c.retObj(f ? v : c.o(1)); return; }
            if (!f || !v) { c.ret = c.args[1]; return; }
            Value r; r.bits = v->nativeHandle; c.ret = r;
        });
    }
    A(SP, "getAll", "()Ljava/util/Map;", [](NC& c) {
        Object* m = c.vm.newInstance("Ljava/util/HashMap;"); auto* md = mapOf(c.vm, m);
        for (auto& e : mapOf(c.vm, c.self)->entries) if (e.live) mapPut(c.vm, md, e.k, e.v);
        c.retObj(m);
    });
    A(SP, "edit", "()Landroid/content/SharedPreferences$Editor;", [](NC& c) {
        Object* e = c.vm.newInstance("Landroid/content/SharedPreferences$Editor;");
        e->setObjField(0xED17, c.self); c.retObj(e);
    });
    A(SP, "registerOnSharedPreferenceChangeListener", "(Landroid/content/SharedPreferences$OnSharedPreferenceChangeListener;)V", [](NC&) {});
    A(SP, "unregisterOnSharedPreferenceChangeListener", "(Landroid/content/SharedPreferences$OnSharedPreferenceChangeListener;)V", [](NC&) {});
    const char* ED = "Landroid/content/SharedPreferences$Editor;";
    auto target = [](NC& c) { return mapOf(c.vm, c.self->getObjField(0xED17)); };
    static const Put eputs[] = {
        {"putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;", 0},
        {"putInt", "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;", 1},
        {"putLong", "(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;", 2},
        {"putBoolean", "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;", 3},
        {"putFloat", "(Ljava/lang/String;F)Landroid/content/SharedPreferences$Editor;", 4},
        {"putStringSet", "(Ljava/lang/String;Ljava/util/Set;)Landroid/content/SharedPreferences$Editor;", 0}};
    for (const auto& p : eputs) {
        int kind = p.kind;
        A(ED, p.name, p.proto, [target, kind](NC& c) {
            Object* v = kind == 0 ? c.o(1) : c.vm.box(boxes[kind], c.args[1].bits);
            mapPut(c.vm, target(c), c.o(0), v); c.retObj(c.self);
        });
    }
    A(ED, "remove", "(Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;", [target](NC& c) { mapRemove(c.vm, target(c), c.o(0)); c.retObj(c.self); });
    A(ED, "clear", "()Landroid/content/SharedPreferences$Editor;", [target](NC& c) { mapClear(target(c)); c.retObj(c.self); });
    A(ED, "apply", "()V", [](NC&) {});
    A(ED, "commit", "()Z", [](NC& c) { c.retBool(true); });

    // ---- Context / Application / Activity --------------------------------------------------------------------------
    const std::vector<std::string> CTX = {"Landroid/content/Context;", "Landroid/content/ContextWrapper;", "Landroid/view/ContextThemeWrapper;",
                                          "Landroid/app/Application;", "Landroid/app/Activity;", "Landroid/app/Service;"};
    M(CTX, "<init>", "()V", [](NC&) {});
    M(CTX, "getPackageName", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(c.vm.props["package"])); });
    M(CTX, "getApplicationContext", "()Landroid/content/Context;", [](NC& c) { c.retObj(g_appContext ? g_appContext : c.self); });
    M(CTX, "getBaseContext", "()Landroid/content/Context;", [](NC& c) { c.retObj(c.self); });
    M(CTX, "getApplication", "()Landroid/app/Application;", [](NC& c) { c.retObj(g_appContext ? g_appContext : c.self); });
    M(CTX, "attachBaseContext", "(Landroid/content/Context;)V", [](NC&) {});
    M(CTX, "getMainLooper", "()Landroid/os/Looper;", [mainLooper](NC& c) { c.retObj(mainLooper(c)); });
    M(CTX, "getClassLoader", "()Ljava/lang/ClassLoader;", [](NC& c) { c.retObj(nullptr); });
    M(CTX, "getSharedPreferences", "(Ljava/lang/String;I)Landroid/content/SharedPreferences;", [prefs](NC& c) { c.retObj(prefs(c)); });
    M(CTX, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", [](NC& c) {
        // A generic object: calls on it resolve to harmless defaults.
        c.retObj(c.vm.newInstance("Ljava/lang/Object;"));
    });
    M(CTX, "checkSelfPermission", "(Ljava/lang/String;)I", [](NC& c) { c.retInt(0); });
    M(CTX, "checkCallingOrSelfPermission", "(Ljava/lang/String;)I", [](NC& c) { c.retInt(0); });
    M(CTX, "startService", "(Landroid/content/Intent;)Landroid/content/ComponentName;", [](NC& c) { c.retObj(nullptr); });
    M(CTX, "sendBroadcast", "(Landroid/content/Intent;)V", [](NC&) {});
    M(CTX, "registerReceiver", "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;", [](NC& c) { c.retObj(nullptr); });
    M(CTX, "unregisterReceiver", "(Landroid/content/BroadcastReceiver;)V", [](NC&) {});
    M(CTX, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;", [](NC& c) { c.retObj(c.vm.newInstance("Landroid/content/pm/ApplicationInfo;")); });
    M(CTX, "getResources", "()Landroid/content/res/Resources;", [](NC& c) {
        static Object* res = nullptr; if (!res) res = c.vm.newInstance("Landroid/content/res/Resources;"); c.retObj(res);
    });

    M({"Landroid/app/Activity;", "Landroid/app/Application;", "Landroid/app/Service;"}, "onCreate", "()V", [](NC&) {});
    A("Landroid/app/Activity;", "onCreate", "(Landroid/os/Bundle;)V", [](NC&) {});
    for (const char* lc : {"onStart", "onResume", "onPause", "onStop", "onDestroy", "onRestart", "onPostCreate", "onPostResume", "onUserLeaveHint", "onAttachedToWindow", "onDetachedFromWindow"}) {
        A("Landroid/app/Activity;", lc, "()V", [](NC&) {});
    }
    A("Landroid/app/Activity;", "onSaveInstanceState", "(Landroid/os/Bundle;)V", [](NC&) {});
    A("Landroid/app/Activity;", "onRestoreInstanceState", "(Landroid/os/Bundle;)V", [](NC&) {});
    A("Landroid/app/Activity;", "onNewIntent", "(Landroid/content/Intent;)V", [](NC&) {});
    A("Landroid/app/Activity;", "onActivityResult", "(IILandroid/content/Intent;)V", [](NC&) {});
    A("Landroid/app/Activity;", "onWindowFocusChanged", "(Z)V", [](NC&) {});
    A("Landroid/app/Activity;", "onConfigurationChanged", "(Landroid/content/res/Configuration;)V", [](NC&) {});
    A("Landroid/app/Activity;", "getIntent", "()Landroid/content/Intent;", [](NC& c) {
        Object* i = c.self->getObjField(0xAC71);
        if (!i) { i = c.vm.newInstance("Landroid/content/Intent;"); mapOf(c.vm, i); c.self->setObjField(0xAC71, i); }
        c.retObj(i);
    });
    A("Landroid/app/Activity;", "setIntent", "(Landroid/content/Intent;)V", [](NC& c) { c.self->setObjField(0xAC71, c.o(0)); });
    A("Landroid/app/Activity;", "finish", "()V", [](NC& c) { c.self->setIntField(0xAC72, 1); if (auto f = c.vm.props.find("hook.finish"); f != c.vm.props.end()) {} });
    A("Landroid/app/Activity;", "isFinishing", "()Z", [](NC& c) { c.retBool(c.self->getIntField(0xAC72) != 0); });
    A("Landroid/app/Activity;", "isDestroyed", "()Z", [](NC& c) { c.retBool(false); });
    A("Landroid/app/Activity;", "runOnUiThread", "(Ljava/lang/Runnable;)V", [](NC& c) {
        Vm* v = &c.vm; Object* r = c.o(0);
        v->post([v, r]() { v->callVirtual(r, "run", "()V"); }, 0);
    });
    A("Landroid/app/Activity;", "setResult", "(I)V", [](NC&) {});
    A("Landroid/app/Activity;", "setResult", "(ILandroid/content/Intent;)V", [](NC&) {});
    A("Landroid/app/Activity;", "overridePendingTransition", "(II)V", [](NC&) {});
    A("Landroid/app/Activity;", "setRequestedOrientation", "(I)V", [](NC&) {});
    A("Landroid/app/Activity;", "requestWindowFeature", "(I)Z", [](NC& c) { c.retBool(true); });
    A("Landroid/app/Activity;", "isTaskRoot", "()Z", [](NC& c) { c.retBool(true); });
    A("Landroid/app/Activity;", "getLayoutInflater", "()Landroid/view/LayoutInflater;", [](NC& c) { c.retObj(c.vm.newInstance("Landroid/view/LayoutInflater;")); });
    A("Landroid/app/Activity;", "getCallingActivity", "()Landroid/content/ComponentName;", [](NC& c) { c.retObj(nullptr); });
    A("Landroid/app/Activity;", "getTitle", "()Ljava/lang/CharSequence;", [](NC& c) { c.retObj(c.vm.newString("")); });
    A("Landroid/app/Activity;", "getComponentName", "()Landroid/content/ComponentName;", [](NC& c) { c.retObj(nullptr); });
    A("Landroid/app/Activity;", "getLocalClassName", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(c.vm.classOf(c.self)->getClassName())); });
    A("Landroid/app/Application;", "onTerminate", "()V", [](NC&) {});
    A("Landroid/app/Application;", "registerActivityLifecycleCallbacks", "(Landroid/app/Application$ActivityLifecycleCallbacks;)V", [](NC&) {});
    A("Landroid/app/Application;", "registerComponentCallbacks", "(Landroid/content/ComponentCallbacks;)V", [](NC&) {});
}

} // namespace NativeDex::Runtime::BytecodeInterpreter
