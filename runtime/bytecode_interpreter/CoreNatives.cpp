// Portable java.lang native class library.

#include "NativeHelpers.h"

#include <atomic>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstdio>
#include <iostream>
#include <thread>

namespace NativeDex::Runtime::BytecodeInterpreter {

namespace {

NativeRegistry& REG() { return NativeRegistry::instance(); }
void A(const char* cls, const char* name, const char* proto, NativeFn fn) {
    REG().add(cls, name, proto, std::move(fn));
}
void decl(const char* cls, const char* super, std::vector<std::string> ifs = {}) {
    REG().declareClass(cls, super, ifs, false);
}
void declI(const char* cls, std::vector<std::string> ifs = {}) {
    REG().declareClass(cls, "Ljava/lang/Object;", ifs, true);
}

int32_t sat32(double d) {
    if (d != d) return 0;
    if (d >= 2147483647.0) return INT_MAX;
    if (d <= -2147483648.0) return INT_MIN;
    return static_cast<int32_t>(d);
}
int64_t sat64(double d) {
    if (d != d) return 0;
    if (d >= 9223372036854775807.0) return LLONG_MAX;
    if (d <= -9223372036854775808.0) return LLONG_MIN;
    return static_cast<int64_t>(d);
}

std::u16string utf8ToU16(const std::string& s) {
    StringObject* tmp = StringObject::create(s);
    std::u16string r = tmp->toUtf16();
    delete tmp;
    return r;
}

Object* newStr(Vm& vm, const std::u16string& s) { return vm.newString(u16ToUtf8(s)); }

} // namespace

// ---------------------------------------------------------------------------
// Shared helpers (declared in NativeHelpers.h)
// ---------------------------------------------------------------------------
std::string u16ToUtf8(const std::u16string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            ++i;
        }
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) { out.push_back(static_cast<char>(0xC0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
        else if (c < 0x10000) { out.push_back(static_cast<char>(0xE0 | (c >> 12))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
        else { out.push_back(static_cast<char>(0xF0 | (c >> 18))); out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
    }
    return out;
}

static std::string javaFP(double d, bool isFloat) {
    if (d != d) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0) return std::signbit(d) ? "-0.0" : "0.0";
    char buf[64];
    std::to_chars_result r = isFloat
        ? std::to_chars(buf, buf + sizeof(buf), static_cast<float>(d), std::chars_format::scientific)
        : std::to_chars(buf, buf + sizeof(buf), d, std::chars_format::scientific);
    std::string sci(buf, r.ptr);
    bool neg = sci[0] == '-';
    if (neg) sci.erase(0, 1);
    size_t e = sci.find('e');
    std::string mant = sci.substr(0, e);
    int exp = std::atoi(sci.c_str() + e + 1);
    std::string digits;
    for (char ch : mant) if (ch != '.') digits.push_back(ch);
    std::string out;
    double ad = std::fabs(d);
    if (ad >= 1e-3 && ad < 1e7) {
        if (exp >= 0) {
            std::string ip = digits.substr(0, std::min<size_t>(digits.size(), exp + 1));
            while (static_cast<int>(ip.size()) < exp + 1) ip.push_back('0');
            std::string fp = digits.size() > static_cast<size_t>(exp + 1) ? digits.substr(exp + 1) : "0";
            out = ip + "." + fp;
        } else {
            out = "0." + std::string(static_cast<size_t>(-exp - 1), '0') + digits;
        }
    } else {
        out = digits.substr(0, 1) + "." + (digits.size() > 1 ? digits.substr(1) : "0") + "E" + std::to_string(exp);
    }
    return neg ? "-" + out : out;
}
std::string javaDoubleToString(double d) { return javaFP(d, false); }
std::string javaFloatToString(float f) { return javaFP(f, true); }

BoxKind boxKindOf(Object* o) {
    if (!o || !o->klass) return BoxKind::None;
    static std::unordered_map<const void*, BoxKind> cache;
    auto it = cache.find(o->klass);
    if (it != cache.end()) return it->second;
    const std::string& d = o->klass->getDescriptor();
    BoxKind k = BoxKind::None;
    if (d == "Ljava/lang/Integer;") k = BoxKind::Int;
    else if (d == "Ljava/lang/Long;") k = BoxKind::Long;
    else if (d == "Ljava/lang/Boolean;") k = BoxKind::Bool;
    else if (d == "Ljava/lang/Byte;") k = BoxKind::Byte;
    else if (d == "Ljava/lang/Character;") k = BoxKind::Char;
    else if (d == "Ljava/lang/Short;") k = BoxKind::Short;
    else if (d == "Ljava/lang/Float;") k = BoxKind::Float;
    else if (d == "Ljava/lang/Double;") k = BoxKind::Double;
    cache[o->klass] = k;
    return k;
}

static double boxDouble(Object* o) {
    uint64_t b = o->nativeHandle;
    switch (boxKindOf(o)) {
        case BoxKind::Float: { uint32_t x = static_cast<uint32_t>(b); float f; std::memcpy(&f, &x, 4); return f; }
        case BoxKind::Double: { double d; std::memcpy(&d, &b, 8); return d; }
        case BoxKind::Long: return static_cast<double>(static_cast<int64_t>(b));
        case BoxKind::Char: return static_cast<double>(static_cast<uint16_t>(b));
        default: return static_cast<double>(static_cast<int32_t>(b));
    }
}
static int64_t boxLong(Object* o) {
    uint64_t b = o->nativeHandle;
    switch (boxKindOf(o)) {
        case BoxKind::Float: case BoxKind::Double: return sat64(boxDouble(o));
        case BoxKind::Long: return static_cast<int64_t>(b);
        case BoxKind::Char: return static_cast<uint16_t>(b);
        case BoxKind::Byte: return static_cast<int8_t>(b);
        case BoxKind::Short: return static_cast<int16_t>(b);
        default: return static_cast<int32_t>(b);
    }
}

std::string boxToString(Object* o) {
    switch (boxKindOf(o)) {
        case BoxKind::Bool: return o->nativeHandle ? "true" : "false";
        case BoxKind::Char: { std::u16string s(1, static_cast<char16_t>(o->nativeHandle)); return u16ToUtf8(s); }
        case BoxKind::Float: return javaFloatToString(static_cast<float>(boxDouble(o)));
        case BoxKind::Double: return javaDoubleToString(boxDouble(o));
        default: return std::to_string(boxLong(o));
    }
}

int32_t boxHash(Object* o) {
    uint64_t b = o->nativeHandle;
    switch (boxKindOf(o)) {
        case BoxKind::Bool: return b ? 1231 : 1237;
        case BoxKind::Long: case BoxKind::Double: return static_cast<int32_t>(b ^ (b >> 32));
        default: return static_cast<int32_t>(static_cast<uint32_t>(b));
    }
}

std::string javaFormat(Vm& vm, const std::string& fmt, ArrayObject* args) {
    std::string out;
    size_t argi = 0;
    auto nextArg = [&]() -> Object* {
        if (!args || argi >= args->length()) return nullptr;
        return args->getObject(argi++);
    };
    for (size_t i = 0; i < fmt.size(); ++i) {
        char ch = fmt[i];
        if (ch != '%') { out.push_back(ch); continue; }
        ++i;
        if (i >= fmt.size()) break;
        std::string flags;
        while (i < fmt.size() && std::strchr("-+ 0,#(", fmt[i])) flags.push_back(fmt[i++]);
        int width = 0;
        while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) width = width * 10 + (fmt[i++] - '0');
        int prec = -1;
        if (i < fmt.size() && fmt[i] == '.') {
            ++i; prec = 0;
            while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) prec = prec * 10 + (fmt[i++] - '0');
        }
        if (i >= fmt.size()) break;
        char conv = fmt[i];
        std::string piece;
        switch (conv) {
            case 'n': piece = "\n"; break;
            case '%': piece = "%"; break;
            case 's': case 'S': {
                piece = vm.javaToString(nextArg());
                if (prec >= 0 && static_cast<int>(piece.size()) > prec) piece.resize(prec);
                if (conv == 'S') for (auto& x : piece) x = static_cast<char>(std::toupper(static_cast<unsigned char>(x)));
                break;
            }
            case 'b': { Object* a = nextArg(); piece = (a && !(boxKindOf(a) == BoxKind::Bool && !a->nativeHandle)) ? "true" : "false"; break; }
            case 'c': { Object* a = nextArg(); piece = a ? boxToString(a) : "null"; break; }
            case 'd': {
                Object* a = nextArg();
                std::string num = a ? std::to_string(boxLong(a)) : "null";
                if (flags.find(',') != std::string::npos && a) {
                    std::string digits = num; bool neg = !digits.empty() && digits[0] == '-';
                    if (neg) digits.erase(0, 1);
                    std::string g;
                    for (size_t k = 0; k < digits.size(); ++k) {
                        if (k && (digits.size() - k) % 3 == 0) g.push_back(',');
                        g.push_back(digits[k]);
                    }
                    num = (neg ? "-" : "") + g;
                }
                if (flags.find('+') != std::string::npos && a && boxLong(a) >= 0) num = "+" + num;
                piece = num;
                break;
            }
            case 'x': case 'X': {
                Object* a = nextArg();
                char buf[32];
                BoxKind k = boxKindOf(a);
                if (k == BoxKind::Long) std::snprintf(buf, sizeof buf, conv == 'x' ? "%llx" : "%llX", static_cast<unsigned long long>(a->nativeHandle));
                else std::snprintf(buf, sizeof buf, conv == 'x' ? "%x" : "%X", static_cast<unsigned>(a ? boxLong(a) : 0));
                piece = buf;
                break;
            }
            case 'f': case 'e': case 'E': case 'g': {
                Object* a = nextArg();
                char spec[16];
                std::snprintf(spec, sizeof spec, "%%.%d%c", prec < 0 ? 6 : prec, conv);
                char buf[512];
                std::snprintf(buf, sizeof buf, spec, a ? boxDouble(a) : 0.0);
                piece = buf;
                if (flags.find('+') != std::string::npos && piece[0] != '-') piece = "+" + piece;
                break;
            }
            default: piece = std::string("%") + conv; break;
        }
        if (width > 0 && static_cast<int>(piece.size()) < width) {
            size_t pad = width - piece.size();
            if (flags.find('-') != std::string::npos) piece += std::string(pad, ' ');
            else if (flags.find('0') != std::string::npos && conv != 's') piece = std::string(pad, '0') + piece;
            else piece = std::string(pad, ' ') + piece;
        }
        out += piece;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
void registerLangNatives() {
    // ---- class hierarchy -------------------------------------------------
    declI("Ljava/lang/CharSequence;");
    declI("Ljava/lang/Comparable;");
    declI("Ljava/lang/Runnable;");
    declI("Ljava/lang/Cloneable;");
    declI("Ljava/io/Serializable;");
    declI("Ljava/lang/AutoCloseable;");
    declI("Ljava/io/Closeable;", {"Ljava/lang/AutoCloseable;"});
    declI("Ljava/lang/Iterable;");
    declI("Ljava/util/concurrent/Callable;");
    declI("Ljava/lang/Appendable;");
    decl("Ljava/lang/String;", "Ljava/lang/Object;", {"Ljava/lang/CharSequence;", "Ljava/lang/Comparable;", "Ljava/io/Serializable;"});
    decl("Ljava/lang/AbstractStringBuilder;", "Ljava/lang/Object;", {"Ljava/lang/CharSequence;", "Ljava/lang/Appendable;"});
    decl("Ljava/lang/StringBuilder;", "Ljava/lang/AbstractStringBuilder;", {"Ljava/lang/CharSequence;"});
    decl("Ljava/lang/StringBuffer;", "Ljava/lang/AbstractStringBuilder;", {"Ljava/lang/CharSequence;"});
    decl("Ljava/lang/Number;", "Ljava/lang/Object;", {"Ljava/io/Serializable;"});
    for (const char* n : {"Integer", "Long", "Short", "Byte", "Float", "Double"}) {
        std::string d = std::string("Ljava/lang/") + n + ";";
        decl(d.c_str(), "Ljava/lang/Number;", {"Ljava/lang/Comparable;"});
    }
    decl("Ljava/lang/Boolean;", "Ljava/lang/Object;", {"Ljava/lang/Comparable;", "Ljava/io/Serializable;"});
    decl("Ljava/lang/Character;", "Ljava/lang/Object;", {"Ljava/lang/Comparable;", "Ljava/io/Serializable;"});
    decl("Ljava/lang/Thread;", "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
    decl("Ljava/lang/Throwable;", "Ljava/lang/Object;", {"Ljava/io/Serializable;"});
    struct Ex { const char* n; const char* s; };
    static const Ex exs[] = {
        {"Exception", "Throwable"}, {"Error", "Throwable"}, {"RuntimeException", "Exception"},
        {"IllegalArgumentException", "RuntimeException"}, {"IllegalStateException", "RuntimeException"},
        {"NullPointerException", "RuntimeException"}, {"ClassCastException", "RuntimeException"},
        {"IndexOutOfBoundsException", "RuntimeException"}, {"ArrayIndexOutOfBoundsException", "IndexOutOfBoundsException"},
        {"StringIndexOutOfBoundsException", "IndexOutOfBoundsException"}, {"ArithmeticException", "RuntimeException"},
        {"NegativeArraySizeException", "RuntimeException"}, {"UnsupportedOperationException", "RuntimeException"},
        {"NumberFormatException", "IllegalArgumentException"}, {"SecurityException", "RuntimeException"},
        {"ArrayStoreException", "RuntimeException"}, {"IllegalMonitorStateException", "RuntimeException"},
        {"ReflectiveOperationException", "Exception"}, {"ClassNotFoundException", "ReflectiveOperationException"},
        {"NoSuchMethodException", "ReflectiveOperationException"}, {"NoSuchFieldException", "ReflectiveOperationException"},
        {"InstantiationException", "ReflectiveOperationException"}, {"IllegalAccessException", "ReflectiveOperationException"},
        {"InterruptedException", "Exception"}, {"CloneNotSupportedException", "Exception"},
        {"LinkageError", "Error"}, {"NoClassDefFoundError", "LinkageError"}, {"ExceptionInInitializerError", "LinkageError"},
        {"IncompatibleClassChangeError", "LinkageError"}, {"AbstractMethodError", "IncompatibleClassChangeError"},
        {"InstantiationError", "IncompatibleClassChangeError"}, {"NoSuchMethodError", "IncompatibleClassChangeError"},
        {"NoSuchFieldError", "IncompatibleClassChangeError"}, {"UnsatisfiedLinkError", "LinkageError"},
        {"VirtualMachineError", "Error"}, {"StackOverflowError", "VirtualMachineError"},
        {"OutOfMemoryError", "VirtualMachineError"}, {"InternalError", "VirtualMachineError"}, {"AssertionError", "Error"},
    };
    for (const auto& e : exs) {
        std::string d = std::string("Ljava/lang/") + e.n + ";";
        std::string s = std::string("Ljava/lang/") + e.s + ";";
        decl(d.c_str(), s.c_str());
    }
    decl("Ljava/io/IOException;", "Ljava/lang/Exception;");
    decl("Ljava/io/FileNotFoundException;", "Ljava/io/IOException;");
    decl("Ljava/io/UnsupportedEncodingException;", "Ljava/io/IOException;");
    decl("Ljava/io/EOFException;", "Ljava/io/IOException;");
    decl("Ljava/util/ConcurrentModificationException;", "Ljava/lang/RuntimeException;");
    decl("Ljava/util/NoSuchElementException;", "Ljava/lang/RuntimeException;");
    decl("Ljava/util/concurrent/TimeoutException;", "Ljava/lang/Exception;");
    decl("Ljava/util/concurrent/ExecutionException;", "Ljava/lang/Exception;");
    decl("Ljava/util/concurrent/CancellationException;", "Ljava/lang/IllegalStateException;");
    decl("Ljava/io/PrintStream;", "Ljava/lang/Object;", {"Ljava/io/Closeable;"});

    // ---- java.lang.Object -----------------------------------------------------
    A("Ljava/lang/Object;", "<init>", "()V", [](NC&) {});
    A("Ljava/lang/Object;", "hashCode", "()I", [](NC& c) { c.retInt(c.vm.identityHash(c.self)); });
    A("Ljava/lang/Object;", "equals", "(Ljava/lang/Object;)Z", [](NC& c) { c.retBool(c.self == c.o(0)); });
    A("Ljava/lang/Object;", "toString", "()Ljava/lang/String;", [](NC& c) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%x", static_cast<unsigned>(c.vm.identityHash(c.self)));
        c.retObj(c.vm.newString(c.vm.classOf(c.self)->getClassName() + "@" + buf));
    });
    A("Ljava/lang/Object;", "getClass", "()Ljava/lang/Class;", [](NC& c) { c.retObj(c.vm.classOf(c.self)); });
    A("Ljava/lang/Object;", "notify", "()V", [](NC& c) { c.vm.monitorNotify(c.self, false); });
    A("Ljava/lang/Object;", "notifyAll", "()V", [](NC& c) { c.vm.monitorNotify(c.self, true); });
    A("Ljava/lang/Object;", "wait", "()V", [](NC& c) { c.vm.monitorWait(c.self, 0); });
    A("Ljava/lang/Object;", "wait", "(J)V", [](NC& c) { c.vm.monitorWait(c.self, std::max<int64_t>(c.j(0), 1)); });
    A("Ljava/lang/Object;", "wait", "(JI)V", [](NC& c) { c.vm.monitorWait(c.self, std::max<int64_t>(c.j(0), 1)); });
    A("Ljava/lang/Object;", "finalize", "()V", [](NC&) {});
    A("Ljava/lang/Object;", "clone", "()Ljava/lang/Object;", [](NC& c) {
        Vm& vm = c.vm;
        if (auto* a = dynamic_cast<ArrayObject*>(c.self)) {
            ArrayObject* n = vm.newArray(a->klass->getDescriptor(), static_cast<int32_t>(a->length()));
            size_t es = a->isObjectArray() ? sizeof(void*) : 0;
            if (!es) {
                switch (a->getElementType()) {
                    case ArrayObject::ArrayType::BYTE: case ArrayObject::ArrayType::BOOLEAN: es = 1; break;
                    case ArrayObject::ArrayType::CHAR: case ArrayObject::ArrayType::SHORT: es = 2; break;
                    case ArrayObject::ArrayType::INT: case ArrayObject::ArrayType::FLOAT: es = 4; break;
                    default: es = 8; break;
                }
            }
            std::memcpy(n->data(), a->data(), es * a->length());
            c.retObj(n);
            return;
        }
        RuntimeClass* k = vm.classOf(c.self);
        Object* n = vm.newInstance(k);
        n->fieldSlots = c.self->fieldSlots;
        n->nativeHandle = c.self->nativeHandle;
        c.retObj(n);
    });

    // ---- java.lang.Class ------------------------------------------------------------
    auto classNameOf = [](RuntimeClass* k) {
        if (k->isArrayClass) { std::string d = k->getDescriptor(); for (auto& ch : d) if (ch == '/') ch = '.'; return d; }
        static const std::unordered_map<std::string, std::string> prim = {
            {"I", "int"}, {"J", "long"}, {"Z", "boolean"}, {"B", "byte"}, {"C", "char"}, {"S", "short"},
            {"F", "float"}, {"D", "double"}, {"V", "void"}};
        auto it = prim.find(k->getDescriptor());
        return it != prim.end() ? it->second : k->getClassName();
    };
    A("Ljava/lang/Class;", "getName", "()Ljava/lang/String;", [classNameOf](NC& c) {
        c.retObj(c.vm.newString(classNameOf(static_cast<RuntimeClass*>(c.self))));
    });
    A("Ljava/lang/Class;", "getCanonicalName", "()Ljava/lang/String;", [classNameOf](NC& c) {
        c.retObj(c.vm.newString(classNameOf(static_cast<RuntimeClass*>(c.self))));
    });
    A("Ljava/lang/Class;", "getSimpleName", "()Ljava/lang/String;", [](NC& c) {
        auto* k = static_cast<RuntimeClass*>(c.self);
        std::string n = k->getSimpleName();
        size_t d = n.find_last_of('$');
        if (d != std::string::npos) n = n.substr(d + 1);
        c.retObj(c.vm.newString(n));
    });
    A("Ljava/lang/Class;", "toString", "()Ljava/lang/String;", [classNameOf](NC& c) {
        auto* k = static_cast<RuntimeClass*>(c.self);
        c.retObj(c.vm.newString((k->isInterfaceClass() ? "interface " : "class ") + classNameOf(k)));
    });
    A("Ljava/lang/Class;", "hashCode", "()I", [](NC& c) { c.retInt(c.vm.identityHash(c.self)); });
    A("Ljava/lang/Class;", "isInstance", "(Ljava/lang/Object;)Z", [](NC& c) {
        c.retBool(c.vm.isInstance(c.o(0), static_cast<RuntimeClass*>(c.self)));
    });
    A("Ljava/lang/Class;", "isAssignableFrom", "(Ljava/lang/Class;)Z", [](NC& c) {
        c.retBool(c.vm.isAssignable(static_cast<RuntimeClass*>(c.o(0)), static_cast<RuntimeClass*>(c.self)));
    });
    A("Ljava/lang/Class;", "getSuperclass", "()Ljava/lang/Class;", [](NC& c) {
        c.retObj(static_cast<RuntimeClass*>(c.self)->parent);
    });
    A("Ljava/lang/Class;", "isInterface", "()Z", [](NC& c) { c.retBool(static_cast<RuntimeClass*>(c.self)->isInterfaceClass()); });
    A("Ljava/lang/Class;", "isArray", "()Z", [](NC& c) { c.retBool(static_cast<RuntimeClass*>(c.self)->isArrayClass); });
    A("Ljava/lang/Class;", "isPrimitive", "()Z", [](NC& c) { c.retBool(static_cast<RuntimeClass*>(c.self)->isPrimitiveClass); });
    A("Ljava/lang/Class;", "isAnonymousClass", "()Z", [](NC& c) { c.retBool(false); });
    A("Ljava/lang/Class;", "isLocalClass", "()Z", [](NC& c) { c.retBool(false); });
    A("Ljava/lang/Class;", "isMemberClass", "()Z", [](NC& c) { c.retBool(false); });
    A("Ljava/lang/Class;", "isEnum", "()Z", [](NC& c) { auto* k = static_cast<RuntimeClass*>(c.self); c.retBool(k->parent && k->parent->getDescriptor() == "Ljava/lang/Enum;"); });
    A("Ljava/lang/Class;", "getEnclosingClass", "()Ljava/lang/Class;", [](NC& c) { c.retObj(nullptr); });
    A("Ljava/lang/Class;", "getPackage", "()Ljava/lang/Package;", [](NC& c) { c.retObj(nullptr); });
    A("Ljava/lang/Class;", "desiredAssertionStatus", "()Z", [](NC& c) { c.retBool(false); });
    A("Ljava/lang/Class;", "getClassLoader", "()Ljava/lang/ClassLoader;", [](NC& c) { c.retObj(nullptr); });
    A("Ljava/lang/Class;", "getModifiers", "()I", [](NC& c) { c.retInt(static_cast<int32_t>(static_cast<RuntimeClass*>(c.self)->getAccessFlags() & 0xFFFF)); });
    A("Ljava/lang/Class;", "cast", "(Ljava/lang/Object;)Ljava/lang/Object;", [](NC& c) { c.retObj(c.o(0)); });
    A("Ljava/lang/Class;", "getComponentType", "()Ljava/lang/Class;", [](NC& c) { c.retObj(static_cast<RuntimeClass*>(c.self)->component); });
    A("Ljava/lang/Class;", "forName", "(Ljava/lang/String;)Ljava/lang/Class;", [](NC& c) {
        std::string n = c.str(0);
        std::string d = "L" + n + ";";
        for (auto& ch : d) if (ch == '.') ch = '/';
        RuntimeClass* k = c.vm.findDexClass(d);
        if (!k) {
            k = c.vm.findClass(d);
            if (!NativeRegistry::instance().findDecl(d) && d != "Ljava/lang/Object;") {
                c.vm.throwNew("Ljava/lang/ClassNotFoundException;", n);
            }
        }
        c.retObj(k);
    });

    // ---- java.lang.Throwable -----------------------------------------------------
    A("Ljava/lang/Throwable;", "<init>", "()V", [](NC& c) {
        auto* d = c.vm.throwableData(c.self); d->trace = c.vm.captureStack();
    });
    A("Ljava/lang/Throwable;", "<init>", "(Ljava/lang/String;)V", [](NC& c) {
        auto* d = c.vm.throwableData(c.self); d->message = c.o(0); d->trace = c.vm.captureStack();
    });
    A("Ljava/lang/Throwable;", "<init>", "(Ljava/lang/String;Ljava/lang/Throwable;)V", [](NC& c) {
        auto* d = c.vm.throwableData(c.self); d->message = c.o(0); d->cause = c.o(1); d->trace = c.vm.captureStack();
    });
    A("Ljava/lang/Throwable;", "<init>", "(Ljava/lang/Throwable;)V", [](NC& c) {
        auto* d = c.vm.throwableData(c.self); d->cause = c.o(0); d->trace = c.vm.captureStack();
        if (c.o(0)) d->message = c.vm.newString(c.vm.javaToString(c.o(0)));
    });
    A("Ljava/lang/Throwable;", "<init>", "(Ljava/lang/String;Ljava/lang/Throwable;ZZ)V", [](NC& c) {
        auto* d = c.vm.throwableData(c.self); d->message = c.o(0); d->cause = c.o(1); d->trace = c.vm.captureStack();
    });
    A("Ljava/lang/Throwable;", "getMessage", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.throwableData(c.self)->message); });
    A("Ljava/lang/Throwable;", "getLocalizedMessage", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.throwableData(c.self)->message); });
    A("Ljava/lang/Throwable;", "getCause", "()Ljava/lang/Throwable;", [](NC& c) { c.retObj(c.vm.throwableData(c.self)->cause); });
    A("Ljava/lang/Throwable;", "initCause", "(Ljava/lang/Throwable;)Ljava/lang/Throwable;", [](NC& c) {
        c.vm.throwableData(c.self)->cause = c.o(0); c.retObj(c.self);
    });
    A("Ljava/lang/Throwable;", "fillInStackTrace", "()Ljava/lang/Throwable;", [](NC& c) {
        c.vm.throwableData(c.self)->trace = c.vm.captureStack(); c.retObj(c.self);
    });
    A("Ljava/lang/Throwable;", "addSuppressed", "(Ljava/lang/Throwable;)V", [](NC&) {});
    A("Ljava/lang/Throwable;", "getSuppressed", "()[Ljava/lang/Throwable;", [](NC& c) { c.retObj(c.vm.newArray("[Ljava/lang/Throwable;", 0)); });
    A("Ljava/lang/Throwable;", "getStackTrace", "()[Ljava/lang/StackTraceElement;", [](NC& c) { c.retObj(c.vm.newArray("[Ljava/lang/StackTraceElement;", 0)); });
    A("Ljava/lang/Throwable;", "setStackTrace", "([Ljava/lang/StackTraceElement;)V", [](NC&) {});
    A("Ljava/lang/Throwable;", "printStackTrace", "()V", [](NC& c) { std::cerr << c.vm.describeThrowable(c.self) << std::endl; });
    A("Ljava/lang/Throwable;", "toString", "()Ljava/lang/String;", [](NC& c) {
        auto* d = c.vm.throwableData(c.self);
        std::string s = c.vm.classOf(c.self)->getClassName();
        if (d->message) s += ": " + c.vm.stringOf(d->message);
        c.retObj(c.vm.newString(s));
    });

    // ---- java.util.UUID -------------------------------------------------------------------
    decl("Ljava/util/UUID;", "Ljava/lang/Object;", {"Ljava/lang/Comparable;", "Ljava/io/Serializable;"});
    auto makeUuid = [](Vm& vm, uint64_t hi, uint64_t lo) {
        Object* u = vm.newInstance("Ljava/util/UUID;");
        u->nativeHandle = hi; u->setWideField(1, lo);
        return u;
    };
    A("Ljava/util/UUID;", "randomUUID", "()Ljava/util/UUID;", [makeUuid](NC& c) {
        static std::atomic<uint64_t> ctr{0x9E3779B97F4A7C15ULL};
        uint64_t a = ctr.fetch_add(0x9E3779B97F4A7C15ULL) ^ static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        uint64_t b = (a ^ (a >> 31)) * 0xBF58476D1CE4E5B9ULL; uint64_t d = (b ^ (b >> 29)) * 0x94D049BB133111EBULL;
        uint64_t hi = (b & ~0xF000ULL) | 0x4000ULL, lo = (d & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
        c.retObj(makeUuid(c.vm, hi, lo));
    });
    A("Ljava/util/UUID;", "<init>", "(JJ)V", [](NC& c) { c.self->nativeHandle = static_cast<uint64_t>(c.j(0)); c.self->setWideField(1, static_cast<uint64_t>(c.j(1))); });
    A("Ljava/util/UUID;", "nameUUIDFromBytes", "([B)Ljava/util/UUID;", [makeUuid](NC& c) {
        auto* a = dynamic_cast<ArrayObject*>(c.o(0)); uint64_t h = 1469598103934665603ULL;
        for (size_t i = 0; a && i < a->length(); ++i) { h ^= static_cast<uint8_t*>(a->data())[i]; h *= 1099511628211ULL; }
        c.retObj(makeUuid(c.vm, (h & ~0xF000ULL) | 0x3000ULL, ((h * 0x9E3779B97F4A7C15ULL) & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL));
    });
    A("Ljava/util/UUID;", "toString", "()Ljava/lang/String;", [](NC& c) {
        uint64_t hi = c.self->nativeHandle, lo = c.self->getWideField(1); char b[48];
        std::snprintf(b, sizeof b, "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(hi >> 32), static_cast<unsigned>((hi >> 16) & 0xFFFF),
                      static_cast<unsigned>(hi & 0xFFFF), static_cast<unsigned>(lo >> 48), static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFULL));
        c.retObj(c.vm.newString(b));
    });
    A("Ljava/util/UUID;", "getMostSignificantBits", "()J", [](NC& c) { c.retLong(static_cast<int64_t>(c.self->nativeHandle)); });
    A("Ljava/util/UUID;", "getLeastSignificantBits", "()J", [](NC& c) { c.retLong(static_cast<int64_t>(c.self->getWideField(1))); });
    A("Ljava/util/UUID;", "hashCode", "()I", [](NC& c) { uint64_t x = c.self->nativeHandle ^ c.self->getWideField(1); c.retInt(static_cast<int32_t>((x >> 32) ^ x)); });
    A("Ljava/util/UUID;", "equals", "(Ljava/lang/Object;)Z", [](NC& c) { Object* o = c.o(0); c.retBool(o && o->klass == c.self->klass && o->nativeHandle == c.self->nativeHandle && o->getWideField(1) == c.self->getWideField(1)); });

    // ---- java.lang.System / Runtime / Math -------------------------------------------
    A("Ljava/lang/System;", "currentTimeMillis", "()J", [](NC& c) {
        c.retLong(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    });
    A("Ljava/lang/System;", "nanoTime", "()J", [](NC& c) {
        c.retLong(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    });
    A("Ljava/lang/System;", "identityHashCode", "(Ljava/lang/Object;)I", [](NC& c) { c.retInt(c.vm.identityHash(c.o(0))); });
    A("Ljava/lang/System;", "lineSeparator", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString("\n")); });
    A("Ljava/lang/System;", "getProperty", "(Ljava/lang/String;)Ljava/lang/String;", [](NC& c) {
        std::string k = c.str(0);
        if (k == "line.separator") c.retObj(c.vm.newString("\n"));
        else if (k == "file.separator") c.retObj(c.vm.newString("/"));
        else if (k == "os.name") c.retObj(c.vm.newString("Linux"));
        else if (k == "java.vm.name") c.retObj(c.vm.newString("Dalvik"));
        else if (k == "os.arch") c.retObj(c.vm.newString("aarch64"));
        else if (k == "os.version") c.retObj(c.vm.newString("5.15.0"));
        else if (k == "java.version") c.retObj(c.vm.newString("0"));
        else if (k == "java.vm.version") c.retObj(c.vm.newString("2.1.0"));
        else if (k == "java.vendor") c.retObj(c.vm.newString("The Android Project"));
        else if (k == "java.specification.version") c.retObj(c.vm.newString("0.9"));
        else if (k == "user.dir") c.retObj(c.vm.newString("/data"));
        else if (k == "user.home") c.retObj(c.vm.newString("/data"));
        else if (k == "java.io.tmpdir") c.retObj(c.vm.newString("/data/cache"));
        else if (k == "http.agent") c.retObj(c.vm.newString("Dalvik/2.1.0 (Linux; U; Android 13)"));
        else c.retObj(nullptr);
    });
    A("Ljava/lang/System;", "getProperty", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", [](NC& c) { c.retObj(c.o(1)); });
    A("Ljava/lang/System;", "getenv", "(Ljava/lang/String;)Ljava/lang/String;", [](NC& c) { c.retObj(nullptr); });
    A("Ljava/lang/System;", "gc", "()V", [](NC&) {});
    A("Ljava/lang/System;", "exit", "(I)V", [](NC& c) { std::cerr << "[Vm] System.exit(" << c.i(0) << ") ignored" << std::endl; });
    A("Ljava/lang/System;", "loadLibrary", "(Ljava/lang/String;)V", [](NC& c) {
        std::cerr << "[Vm] System.loadLibrary(" << c.str(0) << ") ignored (no native libraries)" << std::endl;
    });
    A("Ljava/lang/System;", "load", "(Ljava/lang/String;)V", [](NC&) {});
    A("Ljava/lang/System;", "arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V", [](NC& c) {
        auto* src = dynamic_cast<ArrayObject*>(c.o(0));
        auto* dst = dynamic_cast<ArrayObject*>(c.o(2));
        if (!c.o(0) || !c.o(2)) c.vm.throwNpe("arraycopy");
        if (!src || !dst) c.vm.throwNew("Ljava/lang/ArrayStoreException;", "not an array");
        int32_t sp = c.i(1), dp = c.i(3), n = c.i(4);
        if (sp < 0 || dp < 0 || n < 0 || static_cast<int64_t>(sp) + n > static_cast<int64_t>(src->length()) ||
            static_cast<int64_t>(dp) + n > static_cast<int64_t>(dst->length())) {
            c.vm.throwNew("Ljava/lang/ArrayIndexOutOfBoundsException;",
                          "arraycopy: length " + std::to_string(n) + " out of bounds");
        }
        if (src->getElementType() != dst->getElementType()) c.vm.throwNew("Ljava/lang/ArrayStoreException;", "type mismatch");
        size_t es = 8;
        switch (src->getElementType()) {
            case ArrayObject::ArrayType::BYTE: case ArrayObject::ArrayType::BOOLEAN: es = 1; break;
            case ArrayObject::ArrayType::CHAR: case ArrayObject::ArrayType::SHORT: es = 2; break;
            case ArrayObject::ArrayType::INT: case ArrayObject::ArrayType::FLOAT: es = 4; break;
            default: es = 8; break;
        }
        std::memmove(static_cast<uint8_t*>(dst->data()) + static_cast<size_t>(dp) * es,
                     static_cast<uint8_t*>(src->data()) + static_cast<size_t>(sp) * es, static_cast<size_t>(n) * es);
    });

    A("Ljava/lang/Runtime;", "getRuntime", "()Ljava/lang/Runtime;", [](NC& c) { c.retObj(c.vm.newInstance("Ljava/lang/Runtime;")); });
    A("Ljava/lang/Runtime;", "availableProcessors", "()I", [](NC& c) { c.retInt(4); });
    A("Ljava/lang/Runtime;", "totalMemory", "()J", [](NC& c) { c.retLong(256LL << 20); });
    A("Ljava/lang/Runtime;", "maxMemory", "()J", [](NC& c) { c.retLong(512LL << 20); });
    A("Ljava/lang/Runtime;", "freeMemory", "()J", [](NC& c) { c.retLong(128LL << 20); });
    A("Ljava/lang/Runtime;", "gc", "()V", [](NC&) {});

    A("Ljava/lang/Math;", "abs", "(I)I", [](NC& c) { int32_t v = c.i(0); c.retInt(v < 0 ? static_cast<int32_t>(0u - static_cast<uint32_t>(v)) : v); });
    A("Ljava/lang/Math;", "abs", "(J)J", [](NC& c) { int64_t v = c.j(0); c.retLong(v < 0 ? static_cast<int64_t>(0ull - static_cast<uint64_t>(v)) : v); });
    A("Ljava/lang/Math;", "abs", "(F)F", [](NC& c) { c.retFloat(std::fabs(c.f(0))); });
    A("Ljava/lang/Math;", "abs", "(D)D", [](NC& c) { c.retDouble(std::fabs(c.d(0))); });
    A("Ljava/lang/Math;", "max", "(II)I", [](NC& c) { c.retInt(std::max(c.i(0), c.i(1))); });
    A("Ljava/lang/Math;", "min", "(II)I", [](NC& c) { c.retInt(std::min(c.i(0), c.i(1))); });
    A("Ljava/lang/Math;", "max", "(JJ)J", [](NC& c) { c.retLong(std::max(c.j(0), c.j(1))); });
    A("Ljava/lang/Math;", "min", "(JJ)J", [](NC& c) { c.retLong(std::min(c.j(0), c.j(1))); });
    A("Ljava/lang/Math;", "max", "(FF)F", [](NC& c) { c.retFloat(std::fmax(c.f(0), c.f(1))); });
    A("Ljava/lang/Math;", "min", "(FF)F", [](NC& c) { c.retFloat(std::fmin(c.f(0), c.f(1))); });
    A("Ljava/lang/Math;", "max", "(DD)D", [](NC& c) { c.retDouble(std::fmax(c.d(0), c.d(1))); });
    A("Ljava/lang/Math;", "min", "(DD)D", [](NC& c) { c.retDouble(std::fmin(c.d(0), c.d(1))); });
    A("Ljava/lang/Math;", "pow", "(DD)D", [](NC& c) { c.retDouble(std::pow(c.d(0), c.d(1))); });
    A("Ljava/lang/Math;", "sqrt", "(D)D", [](NC& c) { c.retDouble(std::sqrt(c.d(0))); });
    A("Ljava/lang/Math;", "cbrt", "(D)D", [](NC& c) { c.retDouble(std::cbrt(c.d(0))); });
    A("Ljava/lang/Math;", "floor", "(D)D", [](NC& c) { c.retDouble(std::floor(c.d(0))); });
    A("Ljava/lang/Math;", "ceil", "(D)D", [](NC& c) { c.retDouble(std::ceil(c.d(0))); });
    A("Ljava/lang/Math;", "round", "(F)I", [](NC& c) { c.retInt(sat32(std::floor(static_cast<double>(c.f(0)) + 0.5))); });
    A("Ljava/lang/Math;", "round", "(D)J", [](NC& c) { c.retLong(sat64(std::floor(c.d(0) + 0.5))); });
    A("Ljava/lang/Math;", "sin", "(D)D", [](NC& c) { c.retDouble(std::sin(c.d(0))); });
    A("Ljava/lang/Math;", "cos", "(D)D", [](NC& c) { c.retDouble(std::cos(c.d(0))); });
    A("Ljava/lang/Math;", "tan", "(D)D", [](NC& c) { c.retDouble(std::tan(c.d(0))); });
    A("Ljava/lang/Math;", "atan", "(D)D", [](NC& c) { c.retDouble(std::atan(c.d(0))); });
    A("Ljava/lang/Math;", "atan2", "(DD)D", [](NC& c) { c.retDouble(std::atan2(c.d(0), c.d(1))); });
    A("Ljava/lang/Math;", "exp", "(D)D", [](NC& c) { c.retDouble(std::exp(c.d(0))); });
    A("Ljava/lang/Math;", "log", "(D)D", [](NC& c) { c.retDouble(std::log(c.d(0))); });
    A("Ljava/lang/Math;", "log10", "(D)D", [](NC& c) { c.retDouble(std::log10(c.d(0))); });
    A("Ljava/lang/Math;", "hypot", "(DD)D", [](NC& c) { c.retDouble(std::hypot(c.d(0), c.d(1))); });
    A("Ljava/lang/Math;", "toRadians", "(D)D", [](NC& c) { c.retDouble(c.d(0) / 180.0 * 3.14159265358979323846); });
    A("Ljava/lang/Math;", "random", "()D", [](NC& c) { c.retDouble(static_cast<double>(std::rand()) / (static_cast<double>(RAND_MAX) + 1.0)); });
    A("Ljava/lang/Math;", "floorMod", "(II)I", [](NC& c) {
        int32_t a = c.i(0), b = c.i(1);
        if (b == 0) c.vm.throwNew("Ljava/lang/ArithmeticException;", "divide by zero");
        int32_t m = b == -1 ? 0 : a % b;
        c.retInt((m != 0 && ((m ^ b) < 0)) ? m + b : m);
    });
    A("Ljava/lang/Math;", "floorDiv", "(II)I", [](NC& c) {
        int32_t a = c.i(0), b = c.i(1);
        if (b == 0) c.vm.throwNew("Ljava/lang/ArithmeticException;", "divide by zero");
        if (b == -1) { c.retInt(static_cast<int32_t>(0u - static_cast<uint32_t>(a))); return; }
        int32_t q = a / b;
        c.retInt((a % b != 0 && ((a ^ b) < 0)) ? q - 1 : q);
    });

    // ---- java.io.PrintStream ----------------------------------------------------------
    auto ps = [](NC& c, const std::string& s, bool nl) {
        std::ostream& o = c.self && c.self->nativeHandle == 2 ? std::cerr : std::cout;
        o << s;
        if (nl) o << std::endl;
    };
    for (bool nl : {false, true}) {
        const char* n = nl ? "println" : "print";
        A("Ljava/io/PrintStream;", n, "(Ljava/lang/String;)V", [ps, nl](NC& c) { ps(c, c.s(0) ? c.str(0) : "null", nl); });
        A("Ljava/io/PrintStream;", n, "(Ljava/lang/Object;)V", [ps, nl](NC& c) { ps(c, c.vm.javaToString(c.o(0)), nl); });
        A("Ljava/io/PrintStream;", n, "(I)V", [ps, nl](NC& c) { ps(c, std::to_string(c.i(0)), nl); });
        A("Ljava/io/PrintStream;", n, "(J)V", [ps, nl](NC& c) { ps(c, std::to_string(c.j(0)), nl); });
        A("Ljava/io/PrintStream;", n, "(Z)V", [ps, nl](NC& c) { ps(c, c.z(0) ? "true" : "false", nl); });
        A("Ljava/io/PrintStream;", n, "(C)V", [ps, nl](NC& c) { ps(c, u16ToUtf8(std::u16string(1, static_cast<char16_t>(c.i(0)))), nl); });
        A("Ljava/io/PrintStream;", n, "(D)V", [ps, nl](NC& c) { ps(c, javaDoubleToString(c.d(0)), nl); });
        A("Ljava/io/PrintStream;", n, "(F)V", [ps, nl](NC& c) { ps(c, javaFloatToString(c.f(0)), nl); });
    }
    A("Ljava/io/PrintStream;", "println", "()V", [ps](NC& c) { ps(c, "", true); });
    A("Ljava/io/PrintStream;", "flush", "()V", [](NC&) {});
    A("Ljava/io/PrintStream;", "close", "()V", [](NC&) {});

    // ---- java.lang.String ---------------------------------------------------------------
    const char* S = "Ljava/lang/String;";
    A(S, "<init>", "()V", [](NC& c) { selfString(c)->setUtf8(""); });
    A(S, "<init>", "(Ljava/lang/String;)V", [](NC& c) { selfString(c)->setUtf8(c.str(0)); });
    A(S, "<init>", "([C)V", [](NC& c) {
        auto* a = dynamic_cast<ArrayObject*>(c.o(0));
        if (!a) c.vm.throwNpe("String.<init>");
        selfString(c)->setUtf8(u16ToUtf8(std::u16string(static_cast<const char16_t*>(a->data()), a->length())));
    });
    A(S, "<init>", "([CII)V", [](NC& c) {
        auto* a = dynamic_cast<ArrayObject*>(c.o(0));
        if (!a) c.vm.throwNpe("String.<init>");
        int32_t off = c.i(1), n = c.i(2);
        if (off < 0 || n < 0 || static_cast<size_t>(off) + n > a->length()) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "bad range");
        selfString(c)->setUtf8(u16ToUtf8(std::u16string(static_cast<const char16_t*>(a->data()) + off, n)));
    });
    auto bytesCtor = [](NC& c, int off, int len) {
        auto* a = dynamic_cast<ArrayObject*>(c.o(0));
        if (!a) c.vm.throwNpe("String.<init>");
        if (len < 0) len = static_cast<int>(a->length());
        selfString(c)->setUtf8(std::string(static_cast<const char*>(a->data()) + off, static_cast<size_t>(len)));
    };
    A(S, "<init>", "([B)V", [bytesCtor](NC& c) { bytesCtor(c, 0, -1); });
    A(S, "<init>", "([BLjava/lang/String;)V", [bytesCtor](NC& c) { bytesCtor(c, 0, -1); });
    A(S, "<init>", "([BLjava/nio/charset/Charset;)V", [bytesCtor](NC& c) { bytesCtor(c, 0, -1); });
    A(S, "<init>", "([BII)V", [bytesCtor](NC& c) { bytesCtor(c, c.i(1), c.i(2)); });
    A(S, "<init>", "(Ljava/lang/StringBuilder;)V", [](NC& c) {
        auto* d = c.o(0) ? c.o(0)->nativeAs<SbData>() : nullptr;
        selfString(c)->setUtf8(d ? u16ToUtf8(d->s) : "");
    });
    A(S, "length", "()I", [](NC& c) { c.retInt(static_cast<int32_t>(selfString(c)->u16().size())); });
    A(S, "isEmpty", "()Z", [](NC& c) { c.retBool(selfString(c)->u16().empty()); });
    A(S, "charAt", "(I)C", [](NC& c) {
        const auto& u = selfString(c)->u16();
        int32_t i = c.i(0);
        if (i < 0 || static_cast<size_t>(i) >= u.size()) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "index=" + std::to_string(i) + " length=" + std::to_string(u.size()));
        c.retInt(u[i]);
    });
    A(S, "codePointAt", "(I)I", [](NC& c) {
        const auto& u = selfString(c)->u16();
        int32_t i = c.i(0);
        if (i < 0 || static_cast<size_t>(i) >= u.size()) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", std::to_string(i));
        c.retInt(u[i]);
    });
    A(S, "equals", "(Ljava/lang/Object;)Z", [](NC& c) { auto* o = dynamic_cast<StringObject*>(c.o(0)); c.retBool(o && selfString(c)->equals(o)); });
    A(S, "equalsIgnoreCase", "(Ljava/lang/String;)Z", [](NC& c) {
        auto* o = c.s(0);
        if (!o) { c.retBool(false); return; }
        std::string a = selfString(c)->toUtf8(), b = o->toUtf8();
        c.retBool(a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); }));
    });
    A(S, "hashCode", "()I", [](NC& c) { c.retInt(selfString(c)->hashCode()); });
    A(S, "compareTo", "(Ljava/lang/String;)I", [](NC& c) { if (!c.s(0)) c.vm.throwNpe("compareTo"); c.retInt(selfString(c)->compareTo(c.s(0))); });
    A(S, "compareTo", "(Ljava/lang/Object;)I", [](NC& c) { if (!c.s(0)) c.vm.throwNpe("compareTo"); c.retInt(selfString(c)->compareTo(c.s(0))); });
    A(S, "compareToIgnoreCase", "(Ljava/lang/String;)I", [](NC& c) {
        std::string a = selfString(c)->toUtf8(), b = c.str(0);
        for (auto& x : a) x = static_cast<char>(std::tolower(static_cast<unsigned char>(x)));
        for (auto& x : b) x = static_cast<char>(std::tolower(static_cast<unsigned char>(x)));
        c.retInt(a < b ? -1 : (a > b ? 1 : 0));
    });
    A(S, "toString", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.self); });
    A(S, "intern", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.intern(selfString(c)->toUtf8())); });
    A(S, "concat", "(Ljava/lang/String;)Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(selfString(c)->toUtf8() + c.str(0))); });
    auto substr = [](NC& c, int32_t b, int32_t e) {
        const auto& u = selfString(c)->u16();
        if (b < 0 || e > static_cast<int32_t>(u.size()) || b > e)
            c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "begin " + std::to_string(b) + ", end " + std::to_string(e) + ", length " + std::to_string(u.size()));
        c.retObj(newStr(c.vm, u.substr(b, e - b)));
    };
    A(S, "substring", "(I)Ljava/lang/String;", [substr](NC& c) { substr(c, c.i(0), static_cast<int32_t>(selfString(c)->u16().size())); });
    A(S, "substring", "(II)Ljava/lang/String;", [substr](NC& c) { substr(c, c.i(0), c.i(1)); });
    A(S, "subSequence", "(II)Ljava/lang/CharSequence;", [substr](NC& c) { substr(c, c.i(0), c.i(1)); });
    auto idxOf = [](const std::u16string& h, const std::u16string& n, int32_t from, bool last) -> int32_t {
        if (from < 0) from = 0;
        size_t r = last ? h.rfind(n, from < 0 ? std::u16string::npos : static_cast<size_t>(from)) : h.find(n, static_cast<size_t>(from));
        return r == std::u16string::npos ? -1 : static_cast<int32_t>(r);
    };
    A(S, "indexOf", "(I)I", [idxOf](NC& c) { c.retInt(idxOf(selfString(c)->u16(), std::u16string(1, static_cast<char16_t>(c.i(0))), 0, false)); });
    A(S, "indexOf", "(II)I", [idxOf](NC& c) { c.retInt(idxOf(selfString(c)->u16(), std::u16string(1, static_cast<char16_t>(c.i(0))), c.i(1), false)); });
    A(S, "indexOf", "(Ljava/lang/String;)I", [idxOf](NC& c) { c.retInt(idxOf(selfString(c)->u16(), utf8ToU16(c.str(0)), 0, false)); });
    A(S, "indexOf", "(Ljava/lang/String;I)I", [idxOf](NC& c) { c.retInt(idxOf(selfString(c)->u16(), utf8ToU16(c.str(0)), c.i(1), false)); });
    A(S, "lastIndexOf", "(I)I", [idxOf](NC& c) { c.retInt(idxOf(selfString(c)->u16(), std::u16string(1, static_cast<char16_t>(c.i(0))), INT_MAX, true)); });
    A(S, "lastIndexOf", "(Ljava/lang/String;)I", [idxOf](NC& c) { c.retInt(idxOf(selfString(c)->u16(), utf8ToU16(c.str(0)), INT_MAX, true)); });
    A(S, "contains", "(Ljava/lang/CharSequence;)Z", [](NC& c) { c.retBool(selfString(c)->toUtf8().find(c.vm.javaToString(c.o(0))) != std::string::npos); });
    A(S, "startsWith", "(Ljava/lang/String;)Z", [](NC& c) { std::string a = selfString(c)->toUtf8(), p = c.str(0); c.retBool(a.compare(0, p.size(), p) == 0 && a.size() >= p.size()); });
    A(S, "startsWith", "(Ljava/lang/String;I)Z", [](NC& c) {
        std::string a = selfString(c)->toUtf8(), p = c.str(0); size_t o = static_cast<size_t>(std::max(0, c.i(1)));
        c.retBool(o <= a.size() && a.compare(o, p.size(), p) == 0 && a.size() - o >= p.size());
    });
    A(S, "endsWith", "(Ljava/lang/String;)Z", [](NC& c) { std::string a = selfString(c)->toUtf8(), p = c.str(0); c.retBool(a.size() >= p.size() && a.compare(a.size() - p.size(), p.size(), p) == 0); });
    A(S, "trim", "()Ljava/lang/String;", [](NC& c) {
        std::string a = selfString(c)->toUtf8();
        size_t b = 0, e = a.size();
        while (b < e && static_cast<unsigned char>(a[b]) <= ' ') ++b;
        while (e > b && static_cast<unsigned char>(a[e - 1]) <= ' ') --e;
        c.retObj(c.vm.newString(a.substr(b, e - b)));
    });
    A(S, "isBlank", "()Z", [](NC& c) { std::string a = selfString(c)->toUtf8(); c.retBool(std::all_of(a.begin(), a.end(), [](char x) { return std::isspace(static_cast<unsigned char>(x)) != 0; })); });
    auto caseConv = [](NC& c, bool up) {
        std::string a = selfString(c)->toUtf8();
        for (auto& x : a) x = static_cast<char>(up ? std::toupper(static_cast<unsigned char>(x)) : std::tolower(static_cast<unsigned char>(x)));
        c.retObj(c.vm.newString(a));
    };
    A(S, "toLowerCase", "()Ljava/lang/String;", [caseConv](NC& c) { caseConv(c, false); });
    A(S, "toUpperCase", "()Ljava/lang/String;", [caseConv](NC& c) { caseConv(c, true); });
    A(S, "toLowerCase", "(Ljava/util/Locale;)Ljava/lang/String;", [caseConv](NC& c) { caseConv(c, false); });
    A(S, "toUpperCase", "(Ljava/util/Locale;)Ljava/lang/String;", [caseConv](NC& c) { caseConv(c, true); });
    A(S, "replace", "(CC)Ljava/lang/String;", [](NC& c) {
        std::u16string u = selfString(c)->u16();
        std::replace(u.begin(), u.end(), static_cast<char16_t>(c.i(0)), static_cast<char16_t>(c.i(1)));
        c.retObj(newStr(c.vm, u));
    });
    auto replaceAll = [](NC& c, const std::string& from, const std::string& to) {
        std::string a = selfString(c)->toUtf8();
        if (!from.empty()) {
            size_t p = 0;
            while ((p = a.find(from, p)) != std::string::npos) { a.replace(p, from.size(), to); p += to.size(); }
        }
        c.retObj(c.vm.newString(a));
    };
    A(S, "replace", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Ljava/lang/String;", [replaceAll](NC& c) {
        replaceAll(c, c.vm.javaToString(c.o(0)), c.vm.javaToString(c.o(1)));
    });
    A(S, "replaceAll", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", [replaceAll](NC& c) { replaceAll(c, c.str(0), c.str(1)); });
    A(S, "toCharArray", "()[C", [](NC& c) {
        const auto& u = selfString(c)->u16();
        ArrayObject* a = c.vm.newArray("[C", static_cast<int32_t>(u.size()));
        std::memcpy(a->data(), u.data(), u.size() * 2);
        c.retObj(a);
    });
    auto getBytes = [](NC& c) {
        std::string s = selfString(c)->toUtf8();
        ArrayObject* a = c.vm.newArray("[B", static_cast<int32_t>(s.size()));
        std::memcpy(a->data(), s.data(), s.size());
        c.retObj(a);
    };
    A(S, "getBytes", "()[B", getBytes);
    A(S, "getBytes", "(Ljava/lang/String;)[B", getBytes);
    A(S, "getBytes", "(Ljava/nio/charset/Charset;)[B", getBytes);
    A(S, "matches", "(Ljava/lang/String;)Z", [](NC& c) { c.retBool(selfString(c)->toUtf8() == c.str(0)); });
    A(S, "split", "(Ljava/lang/String;)[Ljava/lang/String;", [](NC& c) {
        std::string a = selfString(c)->toUtf8(), pat = c.str(0);
        std::vector<std::string> parts;
        bool ws = (pat == "\\s+" || pat == " +" || pat == "\\s");
        std::string lit = pat;
        if (lit.size() == 2 && lit[0] == '\\') lit = lit.substr(1);
        if (lit.size() >= 3 && lit.front() == '[' && lit.back() == ']') lit = lit.substr(1, 1);
        if (ws) {
            size_t i = 0;
            while (i <= a.size()) {
                size_t j = i;
                while (j < a.size() && !std::isspace(static_cast<unsigned char>(a[j]))) ++j;
                parts.push_back(a.substr(i, j - i));
                if (j >= a.size()) break;
                i = j;
                while (i < a.size() && std::isspace(static_cast<unsigned char>(a[i]))) ++i;
                if (i >= a.size()) { parts.push_back(""); break; }
            }
        } else if (lit.empty()) {
            for (char ch : a) parts.push_back(std::string(1, ch));
        } else {
            size_t p = 0, q;
            while ((q = a.find(lit, p)) != std::string::npos) { parts.push_back(a.substr(p, q - p)); p = q + lit.size(); }
            parts.push_back(a.substr(p));
        }
        while (parts.size() > 1 && parts.back().empty()) parts.pop_back();
        ArrayObject* arr = c.vm.newArray("[Ljava/lang/String;", static_cast<int32_t>(parts.size()));
        for (size_t i = 0; i < parts.size(); ++i) arr->setObject(i, c.vm.newString(parts[i]));
        c.retObj(arr);
    });
    A(S, "valueOf", "(Ljava/lang/Object;)Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(c.vm.javaToString(c.o(0)))); });
    A(S, "valueOf", "(I)Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(std::to_string(c.i(0)))); });
    A(S, "valueOf", "(J)Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(std::to_string(c.j(0)))); });
    A(S, "valueOf", "(Z)Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(c.z(0) ? "true" : "false")); });
    A(S, "valueOf", "(C)Ljava/lang/String;", [](NC& c) { c.retObj(newStr(c.vm, std::u16string(1, static_cast<char16_t>(c.i(0))))); });
    A(S, "valueOf", "(F)Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(javaFloatToString(c.f(0)))); });
    A(S, "valueOf", "(D)Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(javaDoubleToString(c.d(0)))); });
    A(S, "valueOf", "([C)Ljava/lang/String;", [](NC& c) {
        auto* a = dynamic_cast<ArrayObject*>(c.o(0));
        if (!a) c.vm.throwNpe("valueOf");
        c.retObj(newStr(c.vm, std::u16string(static_cast<const char16_t*>(a->data()), a->length())));
    });
    A(S, "copyValueOf", "([C)Ljava/lang/String;", [](NC& c) {
        auto* a = dynamic_cast<ArrayObject*>(c.o(0));
        if (!a) c.vm.throwNpe("copyValueOf");
        c.retObj(newStr(c.vm, std::u16string(static_cast<const char16_t*>(a->data()), a->length())));
    });
    A(S, "format", "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;", [](NC& c) {
        c.retObj(c.vm.newString(javaFormat(c.vm, c.str(0), dynamic_cast<ArrayObject*>(c.o(1)))));
    });
    A(S, "format", "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;", [](NC& c) {
        c.retObj(c.vm.newString(javaFormat(c.vm, c.str(1), dynamic_cast<ArrayObject*>(c.o(2)))));
    });
    A(S, "join", "(Ljava/lang/CharSequence;[Ljava/lang/CharSequence;)Ljava/lang/String;", [](NC& c) {
        std::string sep = c.vm.javaToString(c.o(0)), out;
        auto* a = dynamic_cast<ArrayObject*>(c.o(1));
        for (size_t i = 0; a && i < a->length(); ++i) { if (i) out += sep; out += c.vm.javaToString(a->getObject(i)); }
        c.retObj(c.vm.newString(out));
    });
    A(S, "join", "(Ljava/lang/CharSequence;Ljava/lang/Iterable;)Ljava/lang/String;", [](NC& c) {
        std::string sep = c.vm.javaToString(c.o(0)), out;
        bool first = true;
        forEachItem(c.vm, c.o(1), [&](Object* o) { if (!first) out += sep; first = false; out += c.vm.javaToString(o); });
        c.retObj(c.vm.newString(out));
    });
    // CharSequence interface on strings / builders
    A("Ljava/lang/CharSequence;", "length", "()I", [](NC& c) { c.retInt(static_cast<int32_t>(utf8ToU16(c.vm.javaToString(c.self)).size())); });
    A("Ljava/lang/CharSequence;", "toString", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(c.vm.javaToString(c.self))); });

    // ---- StringBuilder / StringBuffer ------------------------------------------------------
    const std::vector<std::string> SB = {"Ljava/lang/StringBuilder;", "Ljava/lang/StringBuffer;", "Ljava/lang/AbstractStringBuilder;"};
    auto sbOf = [](NC& c) -> SbData* {
        if (!c.self) c.vm.throwNpe("StringBuilder");
        if (!c.self->native) c.self->native = std::make_unique<SbData>();
        return c.self->nativeAs<SbData>();
    };
    auto REGSB = [&](const char* name, const char* proto, NativeFn fn) { REG().addMulti(SB, name, proto, std::move(fn)); };
    REGSB("<init>", "()V", [sbOf](NC& c) { sbOf(c); });
    REGSB("<init>", "(I)V", [sbOf](NC& c) { sbOf(c); });
    REGSB("<init>", "(Ljava/lang/String;)V", [sbOf](NC& c) { if (!c.o(0)) c.vm.throwNpe("StringBuilder.<init>"); sbOf(c)->s = utf8ToU16(c.str(0)); });
    REGSB("<init>", "(Ljava/lang/CharSequence;)V", [sbOf](NC& c) { sbOf(c)->s = utf8ToU16(c.vm.javaToString(c.o(0))); });
    auto app = [sbOf](NC& c, const std::string& s) { sbOf(c)->s += utf8ToU16(s); c.retObj(c.self); };
    for (const auto& cls : SB) {
        const char* r = cls == "Ljava/lang/StringBuilder;" ? "Ljava/lang/StringBuilder;" : (cls == "Ljava/lang/StringBuffer;" ? "Ljava/lang/StringBuffer;" : "Ljava/lang/AbstractStringBuilder;");
        auto P = [&](const std::string& arg) { return "(" + arg + ")" + r; };
        A(cls.c_str(), "append", P("Ljava/lang/String;").c_str(), [app](NC& c) { app(c, c.o(0) ? c.str(0) : "null"); });
        A(cls.c_str(), "append", P("Ljava/lang/Object;").c_str(), [app](NC& c) { app(c, c.vm.javaToString(c.o(0))); });
        A(cls.c_str(), "append", P("Ljava/lang/CharSequence;").c_str(), [app](NC& c) { app(c, c.vm.javaToString(c.o(0))); });
        A(cls.c_str(), "append", P("Ljava/lang/StringBuffer;").c_str(), [app](NC& c) { app(c, c.vm.javaToString(c.o(0))); });
        A(cls.c_str(), "append", P("I").c_str(), [app](NC& c) { app(c, std::to_string(c.i(0))); });
        A(cls.c_str(), "append", P("J").c_str(), [app](NC& c) { app(c, std::to_string(c.j(0))); });
        A(cls.c_str(), "append", P("Z").c_str(), [app](NC& c) { app(c, c.z(0) ? "true" : "false"); });
        A(cls.c_str(), "append", P("F").c_str(), [app](NC& c) { app(c, javaFloatToString(c.f(0))); });
        A(cls.c_str(), "append", P("D").c_str(), [app](NC& c) { app(c, javaDoubleToString(c.d(0))); });
        A(cls.c_str(), "append", P("C").c_str(), [sbOf](NC& c) { sbOf(c)->s.push_back(static_cast<char16_t>(c.i(0))); c.retObj(c.self); });
        A(cls.c_str(), "append", P("[C").c_str(), [sbOf](NC& c) {
            auto* a = dynamic_cast<ArrayObject*>(c.o(0));
            if (a) sbOf(c)->s.append(static_cast<const char16_t*>(a->data()), a->length());
            c.retObj(c.self);
        });
        A(cls.c_str(), "append", P("Ljava/lang/CharSequence;II").c_str(), [sbOf](NC& c) {
            std::u16string u = utf8ToU16(c.vm.javaToString(c.o(0)));
            int32_t b = c.i(1), e = c.i(2);
            if (b >= 0 && e <= static_cast<int32_t>(u.size()) && b <= e) sbOf(c)->s += u.substr(b, e - b);
            c.retObj(c.self);
        });
        A(cls.c_str(), "insert", P("ILjava/lang/String;").c_str(), [sbOf](NC& c) {
            auto* d = sbOf(c); int32_t p = c.i(0);
            if (p < 0 || static_cast<size_t>(p) > d->s.size()) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "offset " + std::to_string(p));
            d->s.insert(p, utf8ToU16(c.o(1) ? c.str(1) : "null")); c.retObj(c.self);
        });
        A(cls.c_str(), "insert", P("ILjava/lang/Object;").c_str(), [sbOf](NC& c) {
            auto* d = sbOf(c); int32_t p = c.i(0);
            if (p < 0 || static_cast<size_t>(p) > d->s.size()) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "offset " + std::to_string(p));
            d->s.insert(p, utf8ToU16(c.vm.javaToString(c.o(1)))); c.retObj(c.self);
        });
        A(cls.c_str(), "insert", P("IC").c_str(), [sbOf](NC& c) {
            auto* d = sbOf(c); int32_t p = c.i(0);
            if (p < 0 || static_cast<size_t>(p) > d->s.size()) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "offset " + std::to_string(p));
            d->s.insert(d->s.begin() + p, static_cast<char16_t>(c.i(1))); c.retObj(c.self);
        });
        A(cls.c_str(), "insert", P("II").c_str(), [sbOf](NC& c) {
            auto* d = sbOf(c); int32_t p = c.i(0);
            if (p < 0 || static_cast<size_t>(p) > d->s.size()) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "offset " + std::to_string(p));
            d->s.insert(p, utf8ToU16(std::to_string(c.i(1)))); c.retObj(c.self);
        });
        A(cls.c_str(), "reverse", P("").c_str(), [sbOf](NC& c) { auto* d = sbOf(c); std::reverse(d->s.begin(), d->s.end()); c.retObj(c.self); });
        A(cls.c_str(), "deleteCharAt", P("I").c_str(), [sbOf](NC& c) {
            auto* d = sbOf(c); int32_t p = c.i(0);
            if (p < 0 || static_cast<size_t>(p) >= d->s.size()) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "index " + std::to_string(p));
            d->s.erase(p, 1); c.retObj(c.self);
        });
        A(cls.c_str(), "delete", P("II").c_str(), [sbOf](NC& c) {
            auto* d = sbOf(c); int32_t b = c.i(0), e = std::min<int32_t>(c.i(1), static_cast<int32_t>(d->s.size()));
            if (b < 0 || b > e) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "start " + std::to_string(b));
            d->s.erase(b, e - b); c.retObj(c.self);
        });
        A(cls.c_str(), "replace", P("IILjava/lang/String;").c_str(), [sbOf](NC& c) {
            auto* d = sbOf(c); int32_t b = c.i(0), e = std::min<int32_t>(c.i(1), static_cast<int32_t>(d->s.size()));
            if (b < 0 || b > e) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "start " + std::to_string(b));
            d->s.replace(b, e - b, utf8ToU16(c.str(2))); c.retObj(c.self);
        });
    }
    REGSB("toString", "()Ljava/lang/String;", [sbOf](NC& c) { c.retObj(newStr(c.vm, sbOf(c)->s)); });
    REGSB("length", "()I", [sbOf](NC& c) { c.retInt(static_cast<int32_t>(sbOf(c)->s.size())); });
    REGSB("capacity", "()I", [sbOf](NC& c) { c.retInt(static_cast<int32_t>(std::max<size_t>(16, sbOf(c)->s.capacity()))); });
    REGSB("isEmpty", "()Z", [sbOf](NC& c) { c.retBool(sbOf(c)->s.empty()); });
    REGSB("charAt", "(I)C", [sbOf](NC& c) {
        auto* d = sbOf(c); int32_t i = c.i(0);
        if (i < 0 || static_cast<size_t>(i) >= d->s.size()) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "index " + std::to_string(i) + ",length " + std::to_string(d->s.size()));
        c.retInt(d->s[i]);
    });
    REGSB("setCharAt", "(IC)V", [sbOf](NC& c) {
        auto* d = sbOf(c); int32_t i = c.i(0);
        if (i < 0 || static_cast<size_t>(i) >= d->s.size()) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "index " + std::to_string(i));
        d->s[i] = static_cast<char16_t>(c.i(1));
    });
    REGSB("setLength", "(I)V", [sbOf](NC& c) { sbOf(c)->s.resize(static_cast<size_t>(std::max(0, c.i(0))), u'\0'); });
    REGSB("ensureCapacity", "(I)V", [](NC&) {});
    REGSB("indexOf", "(Ljava/lang/String;)I", [sbOf](NC& c) { size_t r = sbOf(c)->s.find(utf8ToU16(c.str(0))); c.retInt(r == std::u16string::npos ? -1 : static_cast<int32_t>(r)); });
    REGSB("substring", "(I)Ljava/lang/String;", [sbOf](NC& c) { auto* d = sbOf(c); int32_t b = c.i(0); if (b < 0 || static_cast<size_t>(b) > d->s.size()) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "start " + std::to_string(b)); c.retObj(newStr(c.vm, d->s.substr(b))); });
    REGSB("substring", "(II)Ljava/lang/String;", [sbOf](NC& c) {
        auto* d = sbOf(c); int32_t b = c.i(0), e = c.i(1);
        if (b < 0 || e > static_cast<int32_t>(d->s.size()) || b > e) c.vm.throwNew("Ljava/lang/StringIndexOutOfBoundsException;", "start " + std::to_string(b) + ", end " + std::to_string(e));
        c.retObj(newStr(c.vm, d->s.substr(b, e - b)));
    });

    // ---- boxed primitives ---------------------------------------------------------------------
    struct BoxDef { const char* cls; const char* prim; const char* name; };
    static const BoxDef boxes[] = {
        {"Ljava/lang/Integer;", "I", "int"}, {"Ljava/lang/Long;", "J", "long"}, {"Ljava/lang/Short;", "S", "short"},
        {"Ljava/lang/Byte;", "B", "byte"}, {"Ljava/lang/Character;", "C", "char"}, {"Ljava/lang/Boolean;", "Z", "boolean"},
        {"Ljava/lang/Float;", "F", "float"}, {"Ljava/lang/Double;", "D", "double"}};
    const std::vector<std::string> NUMS = {"Ljava/lang/Number;", "Ljava/lang/Integer;", "Ljava/lang/Long;", "Ljava/lang/Short;",
                                           "Ljava/lang/Byte;", "Ljava/lang/Float;", "Ljava/lang/Double;"};
    const std::vector<std::string> ALLBOX = {"Ljava/lang/Number;", "Ljava/lang/Integer;", "Ljava/lang/Long;", "Ljava/lang/Short;", "Ljava/lang/Byte;",
                                             "Ljava/lang/Float;", "Ljava/lang/Double;", "Ljava/lang/Boolean;", "Ljava/lang/Character;"};
    for (const auto& b : boxes) {
        std::string cls = b.cls, prim = b.prim;
        A(b.cls, "valueOf", ("(" + prim + ")" + cls).c_str(), [cls](NC& c) {
            if (c.args.empty()) { c.retObj(nullptr); return; }
            c.retObj(c.vm.box(cls, c.args[0].bits & (cls == "Ljava/lang/Long;" || cls == "Ljava/lang/Double;" ? ~0ull : 0xFFFFFFFFull)));
        });
        A(b.cls, "<init>", ("(" + prim + ")V").c_str(), [cls](NC& c) {
            c.self->nativeHandle = c.args[0].bits & (cls == "Ljava/lang/Long;" || cls == "Ljava/lang/Double;" ? ~0ull : 0xFFFFFFFFull);
        });
        A(b.cls, "toString", ("(" + prim + ")Ljava/lang/String;").c_str(), [cls](NC& c) {
            Object* tmp = c.vm.box(cls, c.args[0].bits);
            c.retObj(c.vm.newString(boxToString(tmp)));
        });
        A(b.cls, "hashCode", ("(" + prim + ")I").c_str(), [cls](NC& c) { Object* t = c.vm.box(cls, c.args[0].bits); c.retInt(boxHash(t)); });
        A(b.cls, "compare", ("(" + prim + prim + ")I").c_str(), [cls](NC& c) {
            Object* x = c.vm.box(cls, c.args[0].bits); Object* y = c.vm.box(cls, c.args[1].bits);
            double a = boxDouble(x), bb = boxDouble(y);
            if (boxKindOf(x) == BoxKind::Long) { int64_t p = boxLong(x), q = boxLong(y); c.retInt(p < q ? -1 : (p > q ? 1 : 0)); }
            else c.retInt(a < bb ? -1 : (a > bb ? 1 : 0));
        });
        A(b.cls, "compareTo", ("(" + cls + ")I").c_str(), [](NC& c) {
            Object* y = c.o(0);
            if (!y) c.vm.throwNpe("compareTo");
            if (boxKindOf(c.self) == BoxKind::Long) { int64_t p = boxLong(c.self), q = boxLong(y); c.retInt(p < q ? -1 : (p > q ? 1 : 0)); return; }
            double a = boxDouble(c.self), bb = boxDouble(y);
            c.retInt(a < bb ? -1 : (a > bb ? 1 : 0));
        });
    }
    REG().addMulti(ALLBOX, "compareTo", "(Ljava/lang/Object;)I", [](NC& c) {
        Object* y = c.o(0);
        if (!y) c.vm.throwNpe("compareTo");
        double a = boxDouble(c.self), bb = boxDouble(y);
        if (boxKindOf(c.self) == BoxKind::Long) { int64_t p = boxLong(c.self), q = boxLong(y); c.retInt(p < q ? -1 : (p > q ? 1 : 0)); return; }
        c.retInt(a < bb ? -1 : (a > bb ? 1 : 0));
    });
    REG().addMulti(ALLBOX, "equals", "(Ljava/lang/Object;)Z", [](NC& c) {
        Object* o = c.o(0);
        c.retBool(o && o->klass == c.self->klass && o->nativeHandle == c.self->nativeHandle);
    });
    REG().addMulti(ALLBOX, "hashCode", "()I", [](NC& c) { c.retInt(boxHash(c.self)); });
    REG().addMulti(ALLBOX, "toString", "()Ljava/lang/String;", [](NC& c) { c.retObj(c.vm.newString(boxToString(c.self))); });
    REG().addMulti(NUMS, "intValue", "()I", [](NC& c) { BoxKind k = boxKindOf(c.self); c.retInt(k == BoxKind::Float || k == BoxKind::Double ? sat32(boxDouble(c.self)) : static_cast<int32_t>(boxLong(c.self))); });
    REG().addMulti(NUMS, "longValue", "()J", [](NC& c) { c.retLong(boxLong(c.self)); });
    REG().addMulti(NUMS, "floatValue", "()F", [](NC& c) { c.retFloat(static_cast<float>(boxDouble(c.self))); });
    REG().addMulti(NUMS, "doubleValue", "()D", [](NC& c) { c.retDouble(boxDouble(c.self)); });
    REG().addMulti(NUMS, "shortValue", "()S", [](NC& c) { c.retInt(static_cast<int16_t>(boxLong(c.self))); });
    REG().addMulti(NUMS, "byteValue", "()B", [](NC& c) { c.retInt(static_cast<int8_t>(boxLong(c.self))); });
    A("Ljava/lang/Boolean;", "booleanValue", "()Z", [](NC& c) { c.retBool(c.self->nativeHandle != 0); });
    A("Ljava/lang/Character;", "charValue", "()C", [](NC& c) { c.retInt(static_cast<uint16_t>(c.self->nativeHandle)); });

    auto parseLong = [](NC& c, const std::string& s, int radix, int64_t lo, int64_t hi) -> int64_t {
        auto fail = [&]() { c.vm.throwNew("Ljava/lang/NumberFormatException;", c.o(0) ? "For input string: \"" + s + "\"" : "Cannot parse null string"); };
        if (!c.o(0) || s.empty()) fail();
        size_t i = 0; bool neg = false;
        if (s[0] == '-' || s[0] == '+') { neg = s[0] == '-'; i = 1; if (s.size() == 1) fail(); }
        uint64_t mag = 0;
        for (; i < s.size(); ++i) {
            int d;
            char ch = s[i];
            if (ch >= '0' && ch <= '9') d = ch - '0';
            else if (ch >= 'a' && ch <= 'z') d = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'Z') d = ch - 'A' + 10;
            else { fail(); return 0; }
            if (d >= radix) fail();
            if (mag > (UINT64_MAX - static_cast<uint64_t>(d)) / static_cast<uint64_t>(radix)) fail();
            mag = mag * static_cast<uint64_t>(radix) + static_cast<uint64_t>(d);
        }
        uint64_t limitPos = static_cast<uint64_t>(hi);
        uint64_t limitNeg = static_cast<uint64_t>(-(lo + 1)) + 1ull;
        if (neg ? mag > limitNeg : mag > limitPos) fail();
        return neg ? static_cast<int64_t>(0ull - mag) : static_cast<int64_t>(mag);
    };
    A("Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;)I", [parseLong](NC& c) { c.retInt(static_cast<int32_t>(parseLong(c, c.str(0), 10, INT_MIN, INT_MAX))); });
    A("Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;I)I", [parseLong](NC& c) { c.retInt(static_cast<int32_t>(parseLong(c, c.str(0), c.i(1), INT_MIN, INT_MAX))); });
    A("Ljava/lang/Integer;", "valueOf", "(Ljava/lang/String;)Ljava/lang/Integer;", [parseLong](NC& c) { c.retObj(c.vm.box("Ljava/lang/Integer;", static_cast<uint32_t>(parseLong(c, c.str(0), 10, INT_MIN, INT_MAX)))); });
    A("Ljava/lang/Long;", "parseLong", "(Ljava/lang/String;)J", [parseLong](NC& c) { c.retLong(parseLong(c, c.str(0), 10, LLONG_MIN, LLONG_MAX)); });
    A("Ljava/lang/Long;", "valueOf", "(Ljava/lang/String;)Ljava/lang/Long;", [parseLong](NC& c) { c.retObj(c.vm.box("Ljava/lang/Long;", static_cast<uint64_t>(parseLong(c, c.str(0), 10, LLONG_MIN, LLONG_MAX)))); });
    auto parseD = [](NC& c) -> double {
        std::string s = c.str(0);
        if (!c.o(0)) c.vm.throwNpe("parseDouble");
        char* end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (s.empty() || (end && *end && !(std::isspace(static_cast<unsigned char>(*end))) && *end != 'f' && *end != 'd' && *end != 'F' && *end != 'D'))
            c.vm.throwNew("Ljava/lang/NumberFormatException;", "For input string: \"" + s + "\"");
        return d;
    };
    A("Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D", [parseD](NC& c) { c.retDouble(parseD(c)); });
    A("Ljava/lang/Double;", "valueOf", "(Ljava/lang/String;)Ljava/lang/Double;", [parseD](NC& c) { double d = parseD(c); uint64_t b; std::memcpy(&b, &d, 8); c.retObj(c.vm.box("Ljava/lang/Double;", b)); });
    A("Ljava/lang/Float;", "parseFloat", "(Ljava/lang/String;)F", [parseD](NC& c) { c.retFloat(static_cast<float>(parseD(c))); });
    A("Ljava/lang/Float;", "valueOf", "(Ljava/lang/String;)Ljava/lang/Float;", [parseD](NC& c) { float f = static_cast<float>(parseD(c)); uint32_t b; std::memcpy(&b, &f, 4); c.retObj(c.vm.box("Ljava/lang/Float;", b)); });
    A("Ljava/lang/Boolean;", "parseBoolean", "(Ljava/lang/String;)Z", [](NC& c) { std::string s = c.str(0); for (auto& x : s) x = static_cast<char>(std::tolower(static_cast<unsigned char>(x))); c.retBool(s == "true"); });
    A("Ljava/lang/Boolean;", "valueOf", "(Ljava/lang/String;)Ljava/lang/Boolean;", [](NC& c) { std::string s = c.str(0); for (auto& x : s) x = static_cast<char>(std::tolower(static_cast<unsigned char>(x))); c.retObj(c.vm.box("Ljava/lang/Boolean;", s == "true")); });
    A("Ljava/lang/Integer;", "toString", "(II)Ljava/lang/String;", [](NC& c) {
        int64_t v = c.i(0); int r = c.i(1); if (r < 2 || r > 36) r = 10;
        bool neg = v < 0; uint64_t u = neg ? static_cast<uint64_t>(-v) : static_cast<uint64_t>(v);
        std::string s; do { s.push_back("0123456789abcdefghijklmnopqrstuvwxyz"[u % r]); u /= r; } while (u);
        if (neg) s.push_back('-');
        std::reverse(s.begin(), s.end()); c.retObj(c.vm.newString(s));
    });
    A("Ljava/lang/Integer;", "toHexString", "(I)Ljava/lang/String;", [](NC& c) { char b[16]; std::snprintf(b, sizeof b, "%x", static_cast<unsigned>(c.i(0))); c.retObj(c.vm.newString(b)); });
    A("Ljava/lang/Integer;", "toBinaryString", "(I)Ljava/lang/String;", [](NC& c) {
        uint32_t u = static_cast<uint32_t>(c.i(0)); std::string s; do { s.push_back('0' + (u & 1)); u >>= 1; } while (u);
        std::reverse(s.begin(), s.end()); c.retObj(c.vm.newString(s));
    });
    A("Ljava/lang/Long;", "toHexString", "(J)Ljava/lang/String;", [](NC& c) { char b[32]; std::snprintf(b, sizeof b, "%llx", static_cast<unsigned long long>(c.j(0))); c.retObj(c.vm.newString(b)); });
    A("Ljava/lang/Integer;", "bitCount", "(I)I", [](NC& c) { uint32_t u = static_cast<uint32_t>(c.i(0)); int n = 0; while (u) { n += u & 1; u >>= 1; } c.retInt(n); });
    A("Ljava/lang/Integer;", "signum", "(I)I", [](NC& c) { c.retInt(c.i(0) > 0 ? 1 : (c.i(0) < 0 ? -1 : 0)); });
    A("Ljava/lang/Integer;", "highestOneBit", "(I)I", [](NC& c) { uint32_t u = static_cast<uint32_t>(c.i(0)); uint32_t r = 0; while (u) { r = u; u &= u - 1; } c.retInt(static_cast<int32_t>(r)); });
    A("Ljava/lang/Float;", "floatToIntBits", "(F)I", [](NC& c) { c.retInt(static_cast<int32_t>(static_cast<uint32_t>(c.args[0].bits))); });
    A("Ljava/lang/Float;", "floatToRawIntBits", "(F)I", [](NC& c) { c.retInt(static_cast<int32_t>(static_cast<uint32_t>(c.args[0].bits))); });
    A("Ljava/lang/Float;", "intBitsToFloat", "(I)F", [](NC& c) { c.ret = Value(); c.ret.bits = static_cast<uint32_t>(c.args[0].bits); });
    A("Ljava/lang/Double;", "doubleToLongBits", "(D)J", [](NC& c) { c.retLong(static_cast<int64_t>(c.args[0].bits)); });
    A("Ljava/lang/Double;", "doubleToRawLongBits", "(D)J", [](NC& c) { c.retLong(static_cast<int64_t>(c.args[0].bits)); });
    A("Ljava/lang/Double;", "longBitsToDouble", "(J)D", [](NC& c) { c.ret = Value(); c.ret.bits = c.args[0].bits; });
    A("Ljava/lang/Double;", "isNaN", "(D)Z", [](NC& c) { double d = c.d(0); c.retBool(d != d); });
    A("Ljava/lang/Float;", "isNaN", "(F)Z", [](NC& c) { float d = c.f(0); c.retBool(d != d); });
    A("Ljava/lang/Double;", "isNaN", "()Z", [](NC& c) { double d = boxDouble(c.self); c.retBool(d != d); });
    A("Ljava/lang/Double;", "isInfinite", "(D)Z", [](NC& c) { c.retBool(std::isinf(c.d(0))); });

    // ---- Character ----------------------------------------------------------------------------
    const char* CH = "Ljava/lang/Character;";
    A(CH, "isDigit", "(C)Z", [](NC& c) { int x = c.i(0); c.retBool(x >= '0' && x <= '9'); });
    A(CH, "isDigit", "(I)Z", [](NC& c) { int x = c.i(0); c.retBool(x >= '0' && x <= '9'); });
    A(CH, "isLetter", "(C)Z", [](NC& c) { int x = c.i(0); c.retBool((x < 128 && std::isalpha(x)) || x >= 0xC0); });
    A(CH, "isLetter", "(I)Z", [](NC& c) { int x = c.i(0); c.retBool((x < 128 && std::isalpha(x)) || x >= 0xC0); });
    A(CH, "isLetterOrDigit", "(C)Z", [](NC& c) { int x = c.i(0); c.retBool((x < 128 && std::isalnum(x)) || x >= 0xC0); });
    A(CH, "isLetterOrDigit", "(I)Z", [](NC& c) { int x = c.i(0); c.retBool((x < 128 && std::isalnum(x)) || x >= 0xC0); });
    A(CH, "isWhitespace", "(C)Z", [](NC& c) { int x = c.i(0); c.retBool(x == ' ' || (x >= 9 && x <= 13) || (x >= 0x1C && x <= 0x1F)); });
    A(CH, "isWhitespace", "(I)Z", [](NC& c) { int x = c.i(0); c.retBool(x == ' ' || (x >= 9 && x <= 13) || (x >= 0x1C && x <= 0x1F)); });
    A(CH, "isUpperCase", "(C)Z", [](NC& c) { int x = c.i(0); c.retBool(x < 128 && std::isupper(x)); });
    A(CH, "isLowerCase", "(C)Z", [](NC& c) { int x = c.i(0); c.retBool(x < 128 && std::islower(x)); });
    A(CH, "toUpperCase", "(C)C", [](NC& c) { int x = c.i(0); c.retInt(x < 128 ? std::toupper(x) : x); });
    A(CH, "toLowerCase", "(C)C", [](NC& c) { int x = c.i(0); c.retInt(x < 128 ? std::tolower(x) : x); });
    A(CH, "toUpperCase", "(I)I", [](NC& c) { int x = c.i(0); c.retInt(x < 128 ? std::toupper(x) : x); });
    A(CH, "toLowerCase", "(I)I", [](NC& c) { int x = c.i(0); c.retInt(x < 128 ? std::tolower(x) : x); });
    A(CH, "getNumericValue", "(C)I", [](NC& c) { int x = c.i(0); c.retInt(x >= '0' && x <= '9' ? x - '0' : (std::isalpha(x) ? std::tolower(x) - 'a' + 10 : -1)); });
    A(CH, "digit", "(CI)I", [](NC& c) { int x = c.i(0), r = c.i(1); int d = x >= '0' && x <= '9' ? x - '0' : (x < 128 && std::isalpha(x) ? std::tolower(x) - 'a' + 10 : -1); c.retInt(d >= 0 && d < r ? d : -1); });
    A(CH, "forDigit", "(II)C", [](NC& c) { int d = c.i(0); c.retInt(d < 10 ? '0' + d : 'a' + d - 10); });

    // ---- Thread and synchronization ----------------------------------------------------------------
    auto threadOf = [](NC& c) -> ThreadData* {
        if (!c.self->native) c.self->native = std::make_unique<ThreadData>();
        return c.self->nativeAs<ThreadData>();
    };
    A("Ljava/lang/Thread;", "<init>", "()V", [threadOf](NC& c) { threadOf(c); });
    A("Ljava/lang/Thread;", "<init>", "(Ljava/lang/Runnable;)V", [threadOf](NC& c) { threadOf(c)->target = c.o(0); });
    A("Ljava/lang/Thread;", "<init>", "(Ljava/lang/String;)V", [threadOf](NC& c) { threadOf(c)->name = c.str(0); });
    A("Ljava/lang/Thread;", "<init>", "(Ljava/lang/Runnable;Ljava/lang/String;)V", [threadOf](NC& c) { auto* d = threadOf(c); d->target = c.o(0); d->name = c.str(1); });
    A("Ljava/lang/Thread;", "<init>", "(Ljava/lang/ThreadGroup;Ljava/lang/Runnable;Ljava/lang/String;J)V", [threadOf](NC& c) { auto* d = threadOf(c); d->target = c.o(1); d->name = c.str(2); });
    A("Ljava/lang/Thread;", "<init>", "(Ljava/lang/ThreadGroup;Ljava/lang/Runnable;Ljava/lang/String;)V", [threadOf](NC& c) { auto* d = threadOf(c); d->target = c.o(1); d->name = c.str(2); });
    A("Ljava/lang/Thread;", "currentThread", "()Ljava/lang/Thread;", [](NC& c) {
        auto& st = Vm::ts();
        if (!st.threadObject) {
            st.threadObject = c.vm.newInstance("Ljava/lang/Thread;");
            auto* d = new ThreadData();
            d->name = "main";
            st.threadObject->native.reset(d);
        }
        c.retObj(st.threadObject);
    });
    A("Ljava/lang/Thread;", "run", "()V", [threadOf](NC& c) {
        auto* d = threadOf(c);
        if (d->target) c.vm.callVirtual(d->target, "run", "()V");
    });
    A("Ljava/lang/Thread;", "start", "()V", [threadOf](NC& c) {
        auto* d = threadOf(c);
        if (d->started.exchange(true)) c.vm.throwNew("Ljava/lang/IllegalStateException;", "thread already started");
        Vm* vm = &c.vm;
        Object* self = c.self;
        if (vm->debug()) vm->log("[thread] start " + d->name);
        vm->spawnThread([vm, self, d]() {
            Vm::ts().threadObject = self;
            if (vm->debug()) vm->log("[thread] running " + d->name);
            vm->runGuarded([&] { vm->callVirtual(self, "run", "()V"); }, "Thread " + d->name);
            if (vm->debug()) vm->log("[thread] finished " + d->name);
            d->finished = true;
        });
    });
    A("Ljava/lang/Thread;", "join", "()V", [threadOf](NC& c) {
        auto* d = threadOf(c);
        while (d->started && !d->finished) { Vm::Unlocker u(c.vm); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    });
    A("Ljava/lang/Thread;", "join", "(J)V", [threadOf](NC& c) {
        auto* d = threadOf(c);
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max<int64_t>(c.j(0), 1));
        while (d->started && !d->finished && std::chrono::steady_clock::now() < end) { Vm::Unlocker u(c.vm); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    });
    A("Ljava/lang/Thread;", "isAlive", "()Z", [threadOf](NC& c) { auto* d = threadOf(c); c.retBool(d->started && !d->finished); });
    A("Ljava/lang/Thread;", "getName", "()Ljava/lang/String;", [threadOf](NC& c) { c.retObj(c.vm.newString(threadOf(c)->name)); });
    A("Ljava/lang/Thread;", "setName", "(Ljava/lang/String;)V", [threadOf](NC& c) { threadOf(c)->name = c.str(0); });
    A("Ljava/lang/Thread;", "getId", "()J", [](NC& c) { c.retLong(static_cast<int64_t>(c.vm.identityHash(c.self))); });
    A("Ljava/lang/Thread;", "setDaemon", "(Z)V", [](NC&) {});
    A("Ljava/lang/Thread;", "isDaemon", "()Z", [](NC& c) { c.retBool(false); });
    A("Ljava/lang/Thread;", "setPriority", "(I)V", [](NC&) {});
    A("Ljava/lang/Thread;", "interrupt", "()V", [](NC&) {});
    A("Ljava/lang/Thread;", "isInterrupted", "()Z", [](NC& c) { c.retBool(false); });
    A("Ljava/lang/Thread;", "interrupted", "()Z", [](NC& c) { c.retBool(false); });
    A("Ljava/lang/Thread;", "setUncaughtExceptionHandler", "(Ljava/lang/Thread$UncaughtExceptionHandler;)V", [](NC&) {});
    A("Ljava/lang/Thread;", "sleep", "(J)V", [](NC& c) {
        Vm::Unlocker u(c.vm);
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max<int64_t>(0, c.j(0))));
    });
    A("Ljava/lang/Thread;", "getThreadGroup", "()Ljava/lang/ThreadGroup;", [](NC& c) { c.retObj(nullptr); });
    A("Ljava/lang/Thread;", "getContextClassLoader", "()Ljava/lang/ClassLoader;", [](NC& c) { c.retObj(nullptr); });
    A("Ljava/lang/Thread;", "getStackTrace", "()[Ljava/lang/StackTraceElement;", [](NC& c) { c.retObj(c.vm.newArray("[Ljava/lang/StackTraceElement;", 0)); });
    A("Ljava/lang/Thread;", "getPriority", "()I", [](NC& c) { c.retInt(5); });
    A("Ljava/lang/Thread;", "yield", "()V", [](NC& c) { Vm::Unlocker u(c.vm); std::this_thread::yield(); });

    // java.util.concurrent.locks / atomics (single implementation over native handle)
    decl("Ljava/util/concurrent/locks/ReentrantLock;", "Ljava/lang/Object;");
    A("Ljava/util/concurrent/locks/ReentrantLock;", "<init>", "()V", [](NC&) {});
    A("Ljava/util/concurrent/locks/ReentrantLock;", "<init>", "(Z)V", [](NC&) {});
    A("Ljava/util/concurrent/locks/ReentrantLock;", "lock", "()V", [](NC& c) { c.vm.monitorEnter(c.self); });
    A("Ljava/util/concurrent/locks/ReentrantLock;", "unlock", "()V", [](NC& c) { c.vm.monitorExit(c.self); });
    A("Ljava/util/concurrent/locks/ReentrantLock;", "tryLock", "()Z", [](NC& c) { c.vm.monitorEnter(c.self); c.retBool(true); });
    const std::vector<std::string> AT = {"Ljava/util/concurrent/atomic/AtomicInteger;", "Ljava/util/concurrent/atomic/AtomicLong;", "Ljava/util/concurrent/atomic/AtomicBoolean;"};
    for (const auto& a : AT) decl(a.c_str(), "Ljava/lang/Number;");
    REG().addMulti(AT, "<init>", "()V", [](NC&) {});
    REG().addMulti({AT[0]}, "<init>", "(I)V", [](NC& c) { c.self->nativeHandle = static_cast<uint32_t>(c.i(0)); });
    REG().addMulti({AT[1]}, "<init>", "(J)V", [](NC& c) { c.self->nativeHandle = static_cast<uint64_t>(c.j(0)); });
    REG().addMulti({AT[2]}, "<init>", "(Z)V", [](NC& c) { c.self->nativeHandle = c.z(0) ? 1 : 0; });
    auto I32 = [](NC& c) { return static_cast<int32_t>(c.self->nativeHandle); };
    A(AT[0].c_str(), "get", "()I", [I32](NC& c) { c.retInt(I32(c)); });
    A(AT[0].c_str(), "intValue", "()I", [I32](NC& c) { c.retInt(I32(c)); });
    A(AT[0].c_str(), "set", "(I)V", [](NC& c) { c.self->nativeHandle = static_cast<uint32_t>(c.i(0)); });
    A(AT[0].c_str(), "lazySet", "(I)V", [](NC& c) { c.self->nativeHandle = static_cast<uint32_t>(c.i(0)); });
    A(AT[0].c_str(), "incrementAndGet", "()I", [I32](NC& c) { c.self->nativeHandle = static_cast<uint32_t>(I32(c) + 1); c.retInt(I32(c)); });
    A(AT[0].c_str(), "decrementAndGet", "()I", [I32](NC& c) { c.self->nativeHandle = static_cast<uint32_t>(I32(c) - 1); c.retInt(I32(c)); });
    A(AT[0].c_str(), "getAndIncrement", "()I", [I32](NC& c) { int32_t o = I32(c); c.self->nativeHandle = static_cast<uint32_t>(o + 1); c.retInt(o); });
    A(AT[0].c_str(), "getAndDecrement", "()I", [I32](NC& c) { int32_t o = I32(c); c.self->nativeHandle = static_cast<uint32_t>(o - 1); c.retInt(o); });
    A(AT[0].c_str(), "addAndGet", "(I)I", [I32](NC& c) { c.self->nativeHandle = static_cast<uint32_t>(I32(c) + c.i(0)); c.retInt(I32(c)); });
    A(AT[0].c_str(), "getAndAdd", "(I)I", [I32](NC& c) { int32_t o = I32(c); c.self->nativeHandle = static_cast<uint32_t>(o + c.i(0)); c.retInt(o); });
    A(AT[0].c_str(), "getAndSet", "(I)I", [I32](NC& c) { int32_t o = I32(c); c.self->nativeHandle = static_cast<uint32_t>(c.i(0)); c.retInt(o); });
    A(AT[0].c_str(), "compareAndSet", "(II)Z", [I32](NC& c) { if (I32(c) == c.i(0)) { c.self->nativeHandle = static_cast<uint32_t>(c.i(1)); c.retBool(true); } else c.retBool(false); });
    A(AT[1].c_str(), "get", "()J", [](NC& c) { c.retLong(static_cast<int64_t>(c.self->nativeHandle)); });
    A(AT[1].c_str(), "set", "(J)V", [](NC& c) { c.self->nativeHandle = static_cast<uint64_t>(c.j(0)); });
    A(AT[1].c_str(), "incrementAndGet", "()J", [](NC& c) { c.retLong(static_cast<int64_t>(++c.self->nativeHandle)); });
    A(AT[1].c_str(), "getAndIncrement", "()J", [](NC& c) { c.retLong(static_cast<int64_t>(c.self->nativeHandle++)); });
    A(AT[1].c_str(), "addAndGet", "(J)J", [](NC& c) { c.self->nativeHandle += static_cast<uint64_t>(c.j(0)); c.retLong(static_cast<int64_t>(c.self->nativeHandle)); });
    A(AT[1].c_str(), "getAndAdd", "(J)J", [](NC& c) { int64_t o = static_cast<int64_t>(c.self->nativeHandle); c.self->nativeHandle += static_cast<uint64_t>(c.j(0)); c.retLong(o); });
    A(AT[1].c_str(), "compareAndSet", "(JJ)Z", [](NC& c) { if (static_cast<int64_t>(c.self->nativeHandle) == c.j(0)) { c.self->nativeHandle = static_cast<uint64_t>(c.j(1)); c.retBool(true); } else c.retBool(false); });
    A(AT[2].c_str(), "get", "()Z", [](NC& c) { c.retBool(c.self->nativeHandle != 0); });
    A(AT[2].c_str(), "set", "(Z)V", [](NC& c) { c.self->nativeHandle = c.z(0); });
    A(AT[2].c_str(), "getAndSet", "(Z)Z", [](NC& c) { bool o = c.self->nativeHandle != 0; c.self->nativeHandle = c.z(0); c.retBool(o); });
    A(AT[2].c_str(), "compareAndSet", "(ZZ)Z", [](NC& c) { if ((c.self->nativeHandle != 0) == c.z(0)) { c.self->nativeHandle = c.z(1); c.retBool(true); } else c.retBool(false); });
    decl("Ljava/util/concurrent/atomic/AtomicReference;", "Ljava/lang/Object;");
    const char* AR = "Ljava/util/concurrent/atomic/AtomicReference;";
    auto refOf = [](NC& c) -> RefData* { if (!c.self->native) c.self->native = std::make_unique<RefData>(); return c.self->nativeAs<RefData>(); };
    A(AR, "<init>", "()V", [refOf](NC& c) { refOf(c); });
    A(AR, "<init>", "(Ljava/lang/Object;)V", [refOf](NC& c) { refOf(c)->v = c.o(0); });
    A(AR, "get", "()Ljava/lang/Object;", [refOf](NC& c) { c.retObj(refOf(c)->v); });
    A(AR, "set", "(Ljava/lang/Object;)V", [refOf](NC& c) { refOf(c)->v = c.o(0); });
    A(AR, "getAndSet", "(Ljava/lang/Object;)Ljava/lang/Object;", [refOf](NC& c) { auto* d = refOf(c); Object* o = d->v; d->v = c.o(0); c.retObj(o); });
    A(AR, "compareAndSet", "(Ljava/lang/Object;Ljava/lang/Object;)Z", [refOf](NC& c) { auto* d = refOf(c); if (d->v == c.o(0)) { d->v = c.o(1); c.retBool(true); } else c.retBool(false); });
}

void registerFrameworkNatives();
void registerReflectNatives();

void registerCoreNatives() {
    registerLangNatives();
    registerUtilNatives();
    registerFrameworkNatives();
    registerReflectNatives();
}

} // namespace NativeDex::Runtime::BytecodeInterpreter
