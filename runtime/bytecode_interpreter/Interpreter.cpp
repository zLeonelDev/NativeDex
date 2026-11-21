// Dalvik bytecode interpreter. Instruction formats and opcode numbering follow
// https://source.android.com/docs/core/runtime/dalvik-bytecode exactly.

#include "Vm.h"

#include <climits>
#include <cmath>
#include <cstring>

namespace NativeDex::Runtime::BytecodeInterpreter {

namespace {

inline float asFloat(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
inline uint32_t fromFloat(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }
inline double asDouble(uint64_t b) { double d; std::memcpy(&d, &b, 8); return d; }
inline uint64_t fromDouble(double d) { uint64_t b; std::memcpy(&b, &d, 8); return b; }

inline int32_t f2i(double d) {
    if (d != d) return 0;
    if (d >= 2147483647.0) return INT_MAX;
    if (d <= -2147483648.0) return INT_MIN;
    return static_cast<int32_t>(d);
}
inline int64_t d2l(double d) {
    if (d != d) return 0;
    if (d >= 9223372036854775807.0) return LLONG_MAX;
    if (d <= -9223372036854775808.0) return LLONG_MIN;
    return static_cast<int64_t>(d);
}

inline ArrayObject* asArray(Object* o) {
    if (!o || !o->klass) return nullptr;
    return static_cast<RuntimeClass*>(o->klass)->isArrayClass ? static_cast<ArrayObject*>(o) : nullptr;
}

inline bool regEq(const Reg& a, const Reg& b) {
    if (a.o || b.o) return a.o == b.o;
    return a.v == b.v;
}

inline int32_t s32(const uint16_t* p) {
    return static_cast<int32_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 16));
}

[[noreturn]] void divZero(Vm& vm) {
    vm.throwNew("Ljava/lang/ArithmeticException;", "divide by zero");
}

inline Reg* wide(Reg* R, uint32_t r) { return &R[r]; }
inline uint64_t getW(const Reg* R, uint32_t r) {
    return (static_cast<uint64_t>(R[r + 1].v) << 32) | R[r].v;
}
inline void setW(Reg* R, uint32_t r, uint64_t v) {
    R[r].v = static_cast<uint32_t>(v); R[r].o = nullptr;
    R[r + 1].v = static_cast<uint32_t>(v >> 32); R[r + 1].o = nullptr;
}
inline void setI(Reg* R, uint32_t r, uint32_t v) { R[r].v = v; R[r].o = nullptr; }
inline void setO(Reg* R, uint32_t r, Object* o) { R[r].v = 0; R[r].o = o; }

// k: 0..10 int, 11..21 long, 22..26 float, 27..31 double
void doBinop(Vm& vm, Reg* R, unsigned k, uint32_t d, uint32_t a, uint32_t b) {
    if (k <= 10) {
        uint32_t x = R[a].v, y = R[b].v;
        int32_t sx = static_cast<int32_t>(x), sy = static_cast<int32_t>(y);
        uint32_t r = 0;
        switch (k) {
            case 0: r = x + y; break;
            case 1: r = x - y; break;
            case 2: r = x * y; break;
            case 3: if (sy == 0) divZero(vm); r = (sx == INT_MIN && sy == -1) ? x : static_cast<uint32_t>(sx / sy); break;
            case 4: if (sy == 0) divZero(vm); r = (sy == -1) ? 0u : static_cast<uint32_t>(sx % sy); break;
            case 5: r = x & y; break;
            case 6: r = x | y; break;
            case 7: r = x ^ y; break;
            case 8: r = x << (y & 31); break;
            case 9: r = static_cast<uint32_t>(sx >> (y & 31)); break;
            default: r = x >> (y & 31); break;
        }
        setI(R, d, r);
    } else if (k <= 21) {
        uint64_t x = getW(R, a);
        int64_t sx = static_cast<int64_t>(x);
        unsigned op = k - 11;
        if (op >= 8) {                       // shifts: distance is a single int register
            uint32_t sh = R[b].v & 63;
            uint64_t r = op == 8 ? (x << sh) : op == 9 ? static_cast<uint64_t>(sx >> sh) : (x >> sh);
            setW(R, d, r);
            return;
        }
        uint64_t y = getW(R, b);
        int64_t sy = static_cast<int64_t>(y);
        uint64_t r = 0;
        switch (op) {
            case 0: r = x + y; break;
            case 1: r = x - y; break;
            case 2: r = x * y; break;
            case 3: if (sy == 0) divZero(vm); r = (sx == LLONG_MIN && sy == -1) ? x : static_cast<uint64_t>(sx / sy); break;
            case 4: if (sy == 0) divZero(vm); r = (sy == -1) ? 0ull : static_cast<uint64_t>(sx % sy); break;
            case 5: r = x & y; break;
            case 6: r = x | y; break;
            default: r = x ^ y; break;
        }
        setW(R, d, r);
    } else if (k <= 26) {
        float x = asFloat(R[a].v), y = asFloat(R[b].v), r = 0;
        switch (k - 22) {
            case 0: r = x + y; break;
            case 1: r = x - y; break;
            case 2: r = x * y; break;
            case 3: r = x / y; break;
            default: r = std::fmod(x, y); break;
        }
        setI(R, d, fromFloat(r));
    } else {
        double x = asDouble(getW(R, a)), y = asDouble(getW(R, b)), r = 0;
        switch (k - 27) {
            case 0: r = x + y; break;
            case 1: r = x - y; break;
            case 2: r = x * y; break;
            case 3: r = x / y; break;
            default: r = std::fmod(x, y); break;
        }
        setW(R, d, fromDouble(r));
    }
}

// op: 0 add, 1 rsub, 2 mul, 3 div, 4 rem, 5 and, 6 or, 7 xor, 8 shl, 9 shr, 10 ushr
void doLit(Vm& vm, Reg* R, unsigned op, uint32_t d, int32_t x, int32_t lit) {
    uint32_t ux = static_cast<uint32_t>(x), ul = static_cast<uint32_t>(lit);
    uint32_t r = 0;
    switch (op) {
        case 0: r = ux + ul; break;
        case 1: r = ul - ux; break;
        case 2: r = ux * ul; break;
        case 3: if (lit == 0) divZero(vm); r = (x == INT_MIN && lit == -1) ? ux : static_cast<uint32_t>(x / lit); break;
        case 4: if (lit == 0) divZero(vm); r = (lit == -1) ? 0u : static_cast<uint32_t>(x % lit); break;
        case 5: r = ux & ul; break;
        case 6: r = ux | ul; break;
        case 7: r = ux ^ ul; break;
        case 8: r = ux << (ul & 31); break;
        case 9: r = static_cast<uint32_t>(x >> (ul & 31)); break;
        default: r = ux >> (ul & 31); break;
    }
    setI(R, d, r);
}

struct SyncGuard {
    Vm& vm;
    Object* obj;
    SyncGuard(Vm& v, Object* o) : vm(v), obj(o) { if (obj) vm.monitorEnter(obj); }
    ~SyncGuard() { if (obj) vm.monitorExit(obj); }
};

struct TopGuard {
    Vm::ThreadState& st;
    Frame* prev;
    ~TopGuard() { st.top = prev; }
};

} // namespace

// ---------------------------------------------------------------------------
// Method entry
// ---------------------------------------------------------------------------
Value Vm::executeMethod(const RMethod* m, const Reg* args, size_t nargs, Frame* caller) {
    if (stackRemaining() < 192u * 1024u) {
        throwNew("Ljava/lang/StackOverflowError;", "stack overflow in " + m->name);
    }
    Frame f(m->code, m, m->dex, m->dexIndex);
    uint32_t ins = m->code->insSize;
    uint32_t base = f.numRegs >= ins ? f.numRegs - ins : 0;
    for (size_t i = 0; i < nargs && base + i < f.numRegs; ++i) f.regs[base + i] = args[i];

    ThreadState& st = ts();
    f.previous = caller ? caller : st.top;
    Frame* prevTop = st.top;
    st.top = &f;
    TopGuard guard{st, prevTop};

    if (m->isSynchronized()) {
        Object* mon = m->isStatic() ? static_cast<Object*>(m->owner) : f.regs[base].o;
        SyncGuard sg(*this, mon);
        return execute(f);
    }
    return execute(f);
}

// ---------------------------------------------------------------------------
// Invocation from bytecode
// ---------------------------------------------------------------------------
Value Vm::dispatchInvoke(Frame& f, uint8_t op, uint32_t methodIdx, const uint16_t* list,
                         uint32_t argc) {
    MethodRef& mr = methodRef(f.dexIndex, methodIdx);
    const bool isStatic = (op == 0x71 || op == 0x77);
    const bool isVirtual = (op == 0x6E || op == 0x74 || op == 0x72 || op == 0x78);

    Object* recv = nullptr;
    if (!isStatic) {
        recv = argc ? f.getObjectRegister(list[0]) : nullptr;
        if (!recv) {
            throwNpe("Attempt to invoke " + mr.cls + "->" + mr.name + " on a null object reference");
        }
    }
    if (!mr.declared) mr.declared = findClass(mr.cls);

    Target t;
    if (isVirtual) {
        RuntimeClass* k = classOf(recv);
        if (k == mr.cacheKlass) {
            t = mr.cacheTarget;
        } else {
            t = lookupFrom(k, mr.key, mr.name, mr.proto, true);
            if (!t.valid()) t = lookupFrom(mr.declared, mr.key, mr.name, mr.proto, true);
            mr.cacheKlass = k;
            mr.cacheTarget = t;
        }
    } else {
        if (!mr.directResolved) {
            mr.directTarget = lookupFrom(mr.declared, mr.key, mr.name, mr.proto, true);
            mr.directResolved = true;
        }
        t = mr.directTarget;
        if (isStatic && t.method) ensureInitialized(t.method->owner);
    }

    if (debug_) log("[call] " + mr.cls + "->" + mr.name + mr.proto + (t.valid() ? "" : "  <unresolved>"));

    if (!t.valid()) {
        stubMiss(mr.cls, mr.name, mr.proto);
        return stubReturn(mr.proto);
    }

    ThreadState& st = ts();
    if ((++st.tick & 0x1FFF) == 0) yieldLock();

    if (t.method) {
        Reg stackBuf[24];
        std::vector<Reg> heapBuf;
        Reg* buf = stackBuf;
        if (argc > 24) { heapBuf.resize(argc); buf = heapBuf.data(); }
        for (uint32_t i = 0; i < argc; ++i) buf[i] = f.regs[list[i]];
        return executeMethod(t.method, buf, argc, &f);
    }

    // Native implementation: unpack typed arguments.
    NativeCall nc(*this);
    nc.self = recv;
    nc.ref = &mr;
    uint32_t ri = isStatic ? 0 : 1;
    nc.args.reserve(mr.sig.params.size());
    for (Kind k : mr.sig.params) {
        Value v;
        if (ri >= argc) { nc.args.push_back(v); continue; }
        switch (k) {
            case Kind::Ref: v.ref = f.regs[list[ri]].o; ri += 1; break;
            case Kind::Long: case Kind::Double:
                v.bits = (static_cast<uint64_t>(f.regs[list[ri + 1 < argc ? ri + 1 : ri]].v) << 32) |
                         f.regs[list[ri]].v;
                ri += 2;
                break;
            default: v.bits = f.regs[list[ri]].v; ri += 1; break;
        }
        nc.args.push_back(v);
    }
    st.nativeDepth++;
    try { (*t.native)(nc); } catch (...) { st.nativeDepth--; throw; }
    st.nativeDepth--;
    return nc.ret;
}

bool Vm::handleException(Frame& f, uint32_t throwPc, Object* ex) {
    const DexLoader::CodeItem* ci = f.codeItem;
    if (!ci) return false;
    for (const auto& t : ci->tryItems) {
        if (throwPc < t.startAddr || throwPc >= t.startAddr + t.insnCount) continue;
        if (t.handlerOff >= ci->catchHandlers.size()) continue;
        const auto& h = ci->catchHandlers[t.handlerOff];
        for (const auto& item : h.handlers) {
            RuntimeClass* c = classByType(f.dexIndex, item.typeIdx);
            if (c && isInstance(ex, c)) { f.pc = item.address; return true; }
        }
        if (h.hasCatchAll) { f.pc = h.catchAllHandler; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// The interpreter loop
// ---------------------------------------------------------------------------
Value Vm::execute(Frame& F) {
    const uint16_t* const insns = F.codeItem->insns.data();
    const uint32_t size = static_cast<uint32_t>(F.codeItem->insns.size());
    Reg* const R = F.regs.get();
    ThreadState& st = ts();
    uint32_t pc = 0;
    uint32_t curPc = 0;

#define A8 (static_cast<uint32_t>(inst >> 8))
#define A4 (static_cast<uint32_t>((inst >> 8) & 0xF))
#define B4 (static_cast<uint32_t>(inst >> 12))
#define U1 (insns[pc + 1])
#define BRANCH(off)                                         \
    do {                                                    \
        int32_t o_ = (off);                                 \
        if (o_ <= 0 && ((++st.tick & 0x3FFF) == 0)) yieldLock(); \
        pc = static_cast<uint32_t>(static_cast<int32_t>(pc) + o_); \
    } while (0)

    for (;;) {
        try {
            for (;;) {
                if (pc >= size) {
                    throwNew("Ljava/lang/InternalError;", "pc out of range in " + F.method->name);
                }
                curPc = pc;
                F.pc = pc;
                const uint16_t inst = insns[pc];
                const uint8_t op = static_cast<uint8_t>(inst & 0xFF);

                switch (op) {
                // ---- moves ----
                case 0x00: pc += 1; break;
                case 0x01: case 0x07: R[A4] = R[B4]; pc += 1; break;
                case 0x04: R[A4] = R[B4]; R[A4 + 1] = R[B4 + 1]; pc += 1; break;
                case 0x02: case 0x08: R[A8] = R[U1]; pc += 2; break;
                case 0x05: R[A8] = R[U1]; R[A8 + 1] = R[U1 + 1]; pc += 2; break;
                case 0x03: case 0x09: R[U1] = R[insns[pc + 2]]; pc += 3; break;
                case 0x06: { uint32_t d = U1, s = insns[pc + 2]; R[d] = R[s]; R[d + 1] = R[s + 1]; pc += 3; break; }
                case 0x0A: setI(R, A8, static_cast<uint32_t>(F.lastResult.bits)); pc += 1; break;
                case 0x0B: setW(R, A8, F.lastResult.bits); pc += 1; break;
                case 0x0C: setO(R, A8, F.lastResult.ref); pc += 1; break;
                case 0x0D: setO(R, A8, st.pending); st.pending = nullptr; pc += 1; break;

                // ---- returns ----
                case 0x0E: return Value();
                case 0x0F: { Value v; v.bits = R[A8].v; return v; }
                case 0x10: { Value v; v.bits = getW(R, A8); return v; }
                case 0x11: { Value v; v.ref = R[A8].o; return v; }

                // ---- constants ----
                case 0x12: setI(R, A4, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(inst)) >> 12)); pc += 1; break;
                case 0x13: setI(R, A8, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(U1)))); pc += 2; break;
                case 0x14: setI(R, A8, static_cast<uint32_t>(s32(&insns[pc + 1]))); pc += 3; break;
                case 0x15: setI(R, A8, static_cast<uint32_t>(U1) << 16); pc += 2; break;
                case 0x16: setW(R, A8, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(U1)))); pc += 2; break;
                case 0x17: setW(R, A8, static_cast<uint64_t>(static_cast<int64_t>(s32(&insns[pc + 1])))); pc += 3; break;
                case 0x18: {
                    uint64_t v = static_cast<uint64_t>(insns[pc + 1]) | (static_cast<uint64_t>(insns[pc + 2]) << 16) |
                                 (static_cast<uint64_t>(insns[pc + 3]) << 32) | (static_cast<uint64_t>(insns[pc + 4]) << 48);
                    setW(R, A8, v);
                    pc += 5;
                    break;
                }
                case 0x19: setW(R, A8, static_cast<uint64_t>(U1) << 48); pc += 2; break;
                case 0x1A: setO(R, A8, internedString(F.dexIndex, U1)); pc += 2; break;
                case 0x1B: setO(R, A8, internedString(F.dexIndex, static_cast<uint32_t>(s32(&insns[pc + 1])))); pc += 3; break;
                case 0x1C: setO(R, A8, classByType(F.dexIndex, U1)); pc += 2; break;

                // ---- monitors / casts / arrays ----
                case 0x1D: monitorEnter(R[A8].o); pc += 1; break;
                case 0x1E: if (!R[A8].o) throwNpe("monitor-exit"); monitorExit(R[A8].o); pc += 1; break;
                case 0x1F: {
                    Object* o = R[A8].o;
                    if (o) {
                        RuntimeClass* c = classByType(F.dexIndex, U1);
                        if (c && !c->isNative() && !isInstance(o, c)) {
                            throwNew("Ljava/lang/ClassCastException;",
                                     classOf(o)->getClassName() + " cannot be cast to " + c->getClassName());
                        }
                    }
                    pc += 2;
                    break;
                }
                case 0x20: {
                    RuntimeClass* c = classByType(F.dexIndex, U1);
                    setI(R, A4, isInstance(R[B4].o, c) ? 1u : 0u);
                    pc += 2;
                    break;
                }
                case 0x21: {
                    Object* o = R[B4].o;
                    ArrayObject* a = asArray(o);
                    if (!a) { if (!o) throwNpe("array-length"); throwNew("Ljava/lang/InternalError;", "not an array"); }
                    setI(R, A4, static_cast<uint32_t>(a->length()));
                    pc += 1;
                    break;
                }
                case 0x22: {
                    RuntimeClass* c = classByType(F.dexIndex, U1);
                    ensureInitialized(c);
                    if (c->isInterfaceClass() || (c->getAccessFlags() & ACC_ABSTRACT)) {
                        throwNew("Ljava/lang/InstantiationError;", c->getClassName());
                    }
                    setO(R, A8, newInstance(c));
                    pc += 2;
                    break;
                }
                case 0x23: {
                    RuntimeClass* c = classByType(F.dexIndex, U1);
                    setO(R, A4, newArray(c->getDescriptor(), static_cast<int32_t>(R[B4].v)));
                    pc += 2;
                    break;
                }
                case 0x24: case 0x25: {
                    RuntimeClass* c = classByType(F.dexIndex, U1);
                    uint32_t argc, regs[5], first = 0;
                    bool range = op == 0x25;
                    if (range) { argc = A8; first = insns[pc + 2]; }
                    else {
                        argc = B4;
                        uint16_t u2 = insns[pc + 2];
                        regs[0] = u2 & 0xF; regs[1] = (u2 >> 4) & 0xF; regs[2] = (u2 >> 8) & 0xF;
                        regs[3] = (u2 >> 12) & 0xF; regs[4] = (inst >> 8) & 0xF;
                    }
                    ArrayObject* a = newArray(c->getDescriptor(), static_cast<int32_t>(argc));
                    char et = c->getDescriptor().size() > 1 ? c->getDescriptor()[1] : 'L';
                    for (uint32_t i = 0; i < argc; ++i) {
                        uint32_t r = range ? first + i : regs[i];
                        uint8_t* base = static_cast<uint8_t*>(a->data());
                        switch (et) {
                            case 'L': case '[': reinterpret_cast<Object**>(base)[i] = R[r].o; break;
                            case 'B': case 'Z': base[i] = static_cast<uint8_t>(R[r].v); break;
                            case 'C': case 'S': reinterpret_cast<uint16_t*>(base)[i] = static_cast<uint16_t>(R[r].v); break;
                            default: reinterpret_cast<uint32_t*>(base)[i] = R[r].v; break;
                        }
                    }
                    F.lastResult = Value::fromRef(a);
                    pc += 3;
                    break;
                }
                case 0x26: {
                    ArrayObject* a = asArray(R[A8].o);
                    if (!a) throwNpe("fill-array-data");
                    const uint16_t* p = insns + pc + s32(&insns[pc + 1]);
                    uint32_t width = p[1];
                    uint32_t count = static_cast<uint32_t>(p[2]) | (static_cast<uint32_t>(p[3]) << 16);
                    if (count > a->length()) count = static_cast<uint32_t>(a->length());
                    std::memcpy(a->data(), p + 4, static_cast<size_t>(count) * width);
                    pc += 3;
                    break;
                }
                case 0x27: {
                    Object* ex = R[A8].o;
                    if (!ex) throwNpe("throw null");
                    throw JavaThrow{ex};
                }

                // ---- branches ----
                case 0x28: BRANCH(static_cast<int8_t>(inst >> 8)); break;
                case 0x29: BRANCH(static_cast<int16_t>(U1)); break;
                case 0x2A: BRANCH(s32(&insns[pc + 1])); break;
                case 0x2B: {
                    const uint16_t* p = insns + pc + s32(&insns[pc + 1]);
                    uint32_t n = p[1];
                    int32_t first = s32(&p[2]);
                    int64_t idx = static_cast<int64_t>(static_cast<int32_t>(R[A8].v)) - first;
                    if (idx >= 0 && idx < static_cast<int64_t>(n)) BRANCH(s32(&p[4 + idx * 2]));
                    else pc += 3;
                    break;
                }
                case 0x2C: {
                    const uint16_t* p = insns + pc + s32(&insns[pc + 1]);
                    uint32_t n = p[1];
                    int32_t v = static_cast<int32_t>(R[A8].v);
                    bool hit = false;
                    for (uint32_t i = 0; i < n; ++i) {
                        if (s32(&p[2 + i * 2]) == v) { BRANCH(s32(&p[2 + n * 2 + i * 2])); hit = true; break; }
                    }
                    if (!hit) pc += 3;
                    break;
                }

                // ---- compares ----
                case 0x2D: case 0x2E: {
                    float x = asFloat(R[U1 & 0xFF].v), y = asFloat(R[U1 >> 8].v);
                    int32_t r = (x != x || y != y) ? (op == 0x2D ? -1 : 1) : (x < y ? -1 : (x > y ? 1 : 0));
                    setI(R, A8, static_cast<uint32_t>(r)); pc += 2; break;
                }
                case 0x2F: case 0x30: {
                    double x = asDouble(getW(R, U1 & 0xFF)), y = asDouble(getW(R, U1 >> 8));
                    int32_t r = (x != x || y != y) ? (op == 0x2F ? -1 : 1) : (x < y ? -1 : (x > y ? 1 : 0));
                    setI(R, A8, static_cast<uint32_t>(r)); pc += 2; break;
                }
                case 0x31: {
                    int64_t x = static_cast<int64_t>(getW(R, U1 & 0xFF)), y = static_cast<int64_t>(getW(R, U1 >> 8));
                    setI(R, A8, static_cast<uint32_t>(x < y ? -1 : (x > y ? 1 : 0))); pc += 2; break;
                }

                // ---- if-test ----
                case 0x32: if (regEq(R[A4], R[B4])) BRANCH(static_cast<int16_t>(U1)); else pc += 2; break;
                case 0x33: if (!regEq(R[A4], R[B4])) BRANCH(static_cast<int16_t>(U1)); else pc += 2; break;
                case 0x34: if (static_cast<int32_t>(R[A4].v) < static_cast<int32_t>(R[B4].v)) BRANCH(static_cast<int16_t>(U1)); else pc += 2; break;
                case 0x35: if (static_cast<int32_t>(R[A4].v) >= static_cast<int32_t>(R[B4].v)) BRANCH(static_cast<int16_t>(U1)); else pc += 2; break;
                case 0x36: if (static_cast<int32_t>(R[A4].v) > static_cast<int32_t>(R[B4].v)) BRANCH(static_cast<int16_t>(U1)); else pc += 2; break;
                case 0x37: if (static_cast<int32_t>(R[A4].v) <= static_cast<int32_t>(R[B4].v)) BRANCH(static_cast<int16_t>(U1)); else pc += 2; break;
                case 0x38: if (F.isZero(A8)) BRANCH(static_cast<int16_t>(U1)); else pc += 2; break;
                case 0x39: if (!F.isZero(A8)) BRANCH(static_cast<int16_t>(U1)); else pc += 2; break;
                case 0x3A: if (static_cast<int32_t>(R[A8].v) < 0) BRANCH(static_cast<int16_t>(U1)); else pc += 2; break;
                case 0x3B: if (static_cast<int32_t>(R[A8].v) >= 0) BRANCH(static_cast<int16_t>(U1)); else pc += 2; break;
                case 0x3C: if (static_cast<int32_t>(R[A8].v) > 0) BRANCH(static_cast<int16_t>(U1)); else pc += 2; break;
                case 0x3D: if (static_cast<int32_t>(R[A8].v) <= 0) BRANCH(static_cast<int16_t>(U1)); else pc += 2; break;

                // ---- array get / put ----
                case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4A:
                case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: case 0x50: case 0x51: {
                    uint32_t rB = U1 & 0xFF, rC = U1 >> 8;
                    ArrayObject* a = asArray(R[rB].o);
                    if (!a) throwNpe("array access on null");
                    int32_t idx = static_cast<int32_t>(R[rC].v);
                    if (static_cast<uint32_t>(idx) >= a->length()) {
                        throwNew("Ljava/lang/ArrayIndexOutOfBoundsException;",
                                 "length=" + std::to_string(a->length()) + "; index=" + std::to_string(idx));
                    }
                    uint8_t* base = static_cast<uint8_t*>(a->data());
                    uint32_t d = A8;
                    switch (op) {
                        case 0x44: setI(R, d, reinterpret_cast<uint32_t*>(base)[idx]); break;
                        case 0x45: setW(R, d, reinterpret_cast<uint64_t*>(base)[idx]); break;
                        case 0x46: setO(R, d, reinterpret_cast<Object**>(base)[idx]); break;
                        case 0x47: setI(R, d, base[idx]); break;
                        case 0x48: setI(R, d, static_cast<uint32_t>(static_cast<int32_t>(reinterpret_cast<int8_t*>(base)[idx]))); break;
                        case 0x49: setI(R, d, reinterpret_cast<uint16_t*>(base)[idx]); break;
                        case 0x4A: setI(R, d, static_cast<uint32_t>(static_cast<int32_t>(reinterpret_cast<int16_t*>(base)[idx]))); break;
                        case 0x4B: reinterpret_cast<uint32_t*>(base)[idx] = R[d].v; break;
                        case 0x4C: reinterpret_cast<uint64_t*>(base)[idx] = getW(R, d); break;
                        case 0x4D: reinterpret_cast<Object**>(base)[idx] = R[d].o; break;
                        case 0x4E: case 0x4F: base[idx] = static_cast<uint8_t>(R[d].v); break;
                        default: reinterpret_cast<uint16_t*>(base)[idx] = static_cast<uint16_t>(R[d].v); break;
                    }
                    pc += 2;
                    break;
                }

                // ---- instance fields ----
                case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: case 0x58:
                case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
                    FieldRef& fr = fieldRef(F.dexIndex, U1);
                    if (!fr.resolved) resolveField(fr);
                    Object* obj = R[B4].o;
                    if (!obj) throwNpe((op < 0x59 ? "read of field " : "write to field ") + fr.cls + "." + fr.name + " on a null object reference");
                    const uint32_t a = A4;
                    const bool isGet = op < 0x59;
                    const unsigned variant = isGet ? op - 0x52 : op - 0x59;   // 0 int,1 wide,2 obj,3 bool,4 byte,5 char,6 short
                    if (fr.nativeField) {
                        if (isGet) {
                            if (variant == 2) setO(R, a, obj->getObjField(fr.legacyKey));
                            else if (variant == 1) setW(R, a, obj->getWideField(fr.legacyKey));
                            else setI(R, a, obj->getIntField(fr.legacyKey));
                        } else {
                            if (variant == 2) obj->setObjField(fr.legacyKey, R[a].o);
                            else if (variant == 1) obj->setWideField(fr.legacyKey, getW(R, a));
                            else obj->setIntField(fr.legacyKey, R[a].v);
                        }
                    } else {
                        if (fr.slot >= obj->fieldSlots.size()) obj->fieldSlots.resize(fr.slot + 1);
                        Object::FieldSlot& s = obj->fieldSlots[fr.slot];
                        if (isGet) {
                            if (variant == 2) setO(R, a, s.ref);
                            else if (variant == 1) setW(R, a, s.bits);
                            else setI(R, a, static_cast<uint32_t>(s.bits));
                        } else {
                            switch (variant) {
                                case 2: s.ref = R[a].o; break;
                                case 1: s.bits = getW(R, a); break;
                                case 3: s.bits = R[a].v & 0xFF; break;
                                case 4: s.bits = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(R[a].v))); break;
                                case 5: s.bits = R[a].v & 0xFFFF; break;
                                case 6: s.bits = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(R[a].v))); break;
                                default: s.bits = R[a].v; break;
                            }
                        }
                    }
                    pc += 2;
                    break;
                }

                // ---- static fields ----
                case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66:
                case 0x67: case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: {
                    FieldRef& fr = fieldRef(F.dexIndex, U1);
                    if (!fr.resolved) resolveField(fr);
                    Object::FieldSlot* s;
                    if (fr.nativeField) {
                        s = &nativeStaticSlot(fr);
                    } else {
                        ensureInitialized(fr.declaring);
                        s = &fr.declaring->statics[fr.slot];
                    }
                    const uint32_t a = A8;
                    const bool isGet = op < 0x67;
                    const unsigned variant = isGet ? op - 0x60 : op - 0x67;
                    if (isGet) {
                        if (variant == 2) setO(R, a, s->ref);
                        else if (variant == 1) setW(R, a, s->bits);
                        else setI(R, a, static_cast<uint32_t>(s->bits));
                    } else {
                        switch (variant) {
                            case 2: s->ref = R[a].o; break;
                            case 1: s->bits = getW(R, a); break;
                            case 3: s->bits = R[a].v & 0xFF; break;
                            case 4: s->bits = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(R[a].v))); break;
                            case 5: s->bits = R[a].v & 0xFFFF; break;
                            case 6: s->bits = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(R[a].v))); break;
                            default: s->bits = R[a].v; break;
                        }
                    }
                    pc += 2;
                    break;
                }

                // ---- invokes ----
                case 0x6E: case 0x6F: case 0x70: case 0x71: case 0x72: {
                    uint32_t argc = B4;
                    uint16_t u2 = insns[pc + 2];
                    uint16_t list[5] = {
                        static_cast<uint16_t>(u2 & 0xF), static_cast<uint16_t>((u2 >> 4) & 0xF),
                        static_cast<uint16_t>((u2 >> 8) & 0xF), static_cast<uint16_t>((u2 >> 12) & 0xF),
                        static_cast<uint16_t>((inst >> 8) & 0xF)};
                    F.lastResult = dispatchInvoke(F, op, U1, list, argc > 5 ? 5 : argc);
                    pc += 3;
                    break;
                }
                case 0x74: case 0x75: case 0x76: case 0x77: case 0x78: {
                    uint32_t argc = A8;
                    uint32_t first = insns[pc + 2];
                    uint16_t list[256];
                    for (uint32_t i = 0; i < argc; ++i) list[i] = static_cast<uint16_t>(first + i);
                    F.lastResult = dispatchInvoke(F, op, U1, list, argc);
                    pc += 3;
                    break;
                }

                // ---- unary / conversions ----
                case 0x7B: setI(R, A4, 0u - R[B4].v); pc += 1; break;
                case 0x7C: setI(R, A4, ~R[B4].v); pc += 1; break;
                case 0x7D: setW(R, A4, 0ull - getW(R, B4)); pc += 1; break;
                case 0x7E: setW(R, A4, ~getW(R, B4)); pc += 1; break;
                case 0x7F: setI(R, A4, R[B4].v ^ 0x80000000u); pc += 1; break;
                case 0x80: setW(R, A4, getW(R, B4) ^ 0x8000000000000000ull); pc += 1; break;
                case 0x81: setW(R, A4, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(R[B4].v)))); pc += 1; break;
                case 0x82: setI(R, A4, fromFloat(static_cast<float>(static_cast<int32_t>(R[B4].v)))); pc += 1; break;
                case 0x83: setW(R, A4, fromDouble(static_cast<double>(static_cast<int32_t>(R[B4].v)))); pc += 1; break;
                case 0x84: setI(R, A4, static_cast<uint32_t>(getW(R, B4))); pc += 1; break;
                case 0x85: setI(R, A4, fromFloat(static_cast<float>(static_cast<int64_t>(getW(R, B4))))); pc += 1; break;
                case 0x86: setW(R, A4, fromDouble(static_cast<double>(static_cast<int64_t>(getW(R, B4))))); pc += 1; break;
                case 0x87: setI(R, A4, static_cast<uint32_t>(f2i(asFloat(R[B4].v)))); pc += 1; break;
                case 0x88: setW(R, A4, static_cast<uint64_t>(d2l(asFloat(R[B4].v)))); pc += 1; break;
                case 0x89: setW(R, A4, fromDouble(static_cast<double>(asFloat(R[B4].v)))); pc += 1; break;
                case 0x8A: setI(R, A4, static_cast<uint32_t>(f2i(asDouble(getW(R, B4))))); pc += 1; break;
                case 0x8B: setW(R, A4, static_cast<uint64_t>(d2l(asDouble(getW(R, B4))))); pc += 1; break;
                case 0x8C: setI(R, A4, fromFloat(static_cast<float>(asDouble(getW(R, B4))))); pc += 1; break;
                case 0x8D: setI(R, A4, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(R[B4].v)))); pc += 1; break;
                case 0x8E: setI(R, A4, R[B4].v & 0xFFFF); pc += 1; break;
                case 0x8F: setI(R, A4, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(R[B4].v)))); pc += 1; break;

                // ---- three-register binary ops ----
                default:
                    if (op >= 0x90 && op <= 0xAF) {
                        doBinop(*this, R, op - 0x90u, A8, U1 & 0xFF, U1 >> 8);
                        pc += 2;
                    } else if (op >= 0xB0 && op <= 0xCF) {
                        doBinop(*this, R, op - 0xB0u, A4, A4, B4);
                        pc += 1;
                    } else if (op >= 0xD0 && op <= 0xD7) {
                        doLit(*this, R, op - 0xD0u, A4, static_cast<int32_t>(R[B4].v), static_cast<int16_t>(U1));
                        pc += 2;
                    } else if (op >= 0xD8 && op <= 0xE2) {
                        doLit(*this, R, op - 0xD8u, A8, static_cast<int32_t>(R[U1 & 0xFF].v),
                              static_cast<int8_t>(U1 >> 8));
                        pc += 2;
                    } else if (op == 0xFA || op == 0xFB || op == 0xFC || op == 0xFD) {
                        throwNew("Ljava/lang/UnsupportedOperationException;",
                                 "invoke-polymorphic/custom is not supported");
                    } else if (op == 0xFE || op == 0xFF) {
                        setO(R, A8, nullptr);
                        pc += 2;
                    } else {
                        throwNew("Ljava/lang/InternalError;",
                                 "unsupported opcode 0x" + std::to_string(op) + " in " + F.method->name);
                    }
                    break;
                }
            }
        } catch (JavaThrow& t) {
            if (!handleException(F, curPc, t.ex)) throw;
            st.pending = t.ex;
            pc = F.pc;
        }
    }
#undef A8
#undef A4
#undef B4
#undef U1
#undef BRANCH
}

} // namespace NativeDex::Runtime::BytecodeInterpreter
