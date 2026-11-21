#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "Frame.h"

namespace NativeDex::Runtime::ObjectModel {
class StringObject;
}

namespace NativeDex::Runtime::BytecodeInterpreter {

class Vm;
struct MethodRef;

// Argument/return carrier handed to native (C++) implementations of Java
// methods. Arguments are indexed by *parameter*, not by register: a long or
// double is a single argument. The receiver, when there is one, is `self`.
struct NativeCall {
    Vm& vm;
    Object* self = nullptr;
    std::vector<Value> args;
    Value ret;
    const MethodRef* ref = nullptr;

    explicit NativeCall(Vm& v) : vm(v) {}

    int32_t i(size_t k) const { return k < args.size() ? args[k].i32() : 0; }
    bool z(size_t k) const { return i(k) != 0; }
    int64_t j(size_t k) const { return k < args.size() ? args[k].i64() : 0; }
    float f(size_t k) const {
        uint32_t b = k < args.size() ? static_cast<uint32_t>(args[k].bits) : 0;
        float x; std::memcpy(&x, &b, 4); return x;
    }
    double d(size_t k) const {
        uint64_t b = k < args.size() ? args[k].bits : 0;
        double x; std::memcpy(&x, &b, 8); return x;
    }
    Object* o(size_t k) const { return k < args.size() ? args[k].ref : nullptr; }
    ObjectModel::StringObject* s(size_t k) const;     // null if not a String
    std::string str(size_t k) const;                  // "" for null / non-String

    void retInt(int32_t v) { ret = Value::fromInt(v); }
    void retBool(bool v) { ret = Value::fromInt(v ? 1 : 0); }
    void retLong(int64_t v) { ret = Value::fromLong(v); }
    void retFloat(float v) { uint32_t b; std::memcpy(&b, &v, 4); ret = Value(); ret.bits = b; }
    void retDouble(double v) { uint64_t b; std::memcpy(&b, &v, 8); ret = Value(); ret.bits = b; }
    void retObj(Object* r) { ret = Value::fromRef(r); }
};

using NativeFn = std::function<void(NativeCall&)>;

// Process-wide table of native method implementations, keyed by
// (class descriptor, method name, prototype). The portable java.* core lives
// in CoreNatives.cpp; the launcher adds android.* on top of the same table.
class NativeRegistry {
public:
    static NativeRegistry& instance();

    void add(const std::string& cls, const std::string& name,
             const std::string& proto, NativeFn fn);
    // Registers the same implementation for several classes.
    void addMulti(const std::vector<std::string>& classes, const std::string& name,
                  const std::string& proto, NativeFn fn);

    size_t count() const { return table_.size(); }
    const NativeFn* find(const std::string& cls, const std::string& name,
                         const std::string& proto) const;

    // Initial values for static fields of framework classes
    // ("Landroid/os/Build$VERSION;" , "SDK_INT").
    void setStaticInt(const std::string& cls, const std::string& field, int64_t v);
    bool getStaticInt(const std::string& cls, const std::string& field, int64_t& out) const;

    // Static String constants of framework classes (Build.MODEL, ...). The
    // third argument is only used as an out-parameter for the value.
    void setStaticString(const std::string& cls, const std::string& field, const std::string& v);
    bool getStaticString(const std::string& cls, const std::string& field, std::string& out) const;
    // Ordinal of a framework enum constant (declared order); unknown constants
    // get consecutive ordinals on first use.
    void setEnumOrder(const std::string& cls, const std::vector<std::string>& names);
    int32_t enumOrdinal(const std::string& cls, const std::string& name);

    // Declares a framework class: its superclass and interfaces. Used for
    // virtual dispatch and instanceof/catch matching on framework types.
    void declareClass(const std::string& cls, const std::string& super,
                      const std::vector<std::string>& interfaces = {},
                      bool isInterface = false);
    struct ClassDecl {
        std::string super;
        std::vector<std::string> interfaces;
        bool isInterface = false;
    };
    const ClassDecl* findDecl(const std::string& cls) const;

    // Directory for the miss/hit logs ("logs" by default).
    void setLogDir(const std::string& dir) { logDir_ = dir; }
    void logMiss(const std::string& line);

private:
    struct Key {
        std::string cls, name, proto;
        bool operator==(const Key& o) const {
            return cls == o.cls && name == o.name && proto == o.proto;
        }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const noexcept {
            std::hash<std::string> h;
            return h(k.cls) ^ (h(k.name) * 0x9E3779B97F4A7C15ULL) ^ (h(k.proto) * 0xBF58476D1CE4E5B9ULL);
        }
    };
    std::unordered_map<Key, NativeFn, KeyHash> table_;
    std::unordered_map<std::string, int64_t> staticInts_;
    std::unordered_map<std::string, std::string> staticStrings_;
    std::unordered_map<std::string, std::vector<std::string>> enumOrders_;
    std::unordered_map<std::string, ClassDecl> decls_;
    std::string logDir_ = "logs";
};

} // namespace NativeDex::Runtime::BytecodeInterpreter
