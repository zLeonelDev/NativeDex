#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../dex_loader/DexFile.h"
#include "../object_model/Object.h"

namespace NativeDex::Runtime::BytecodeInterpreter {

using ObjectModel::Object;

struct RMethod;

// One Dalvik virtual register. A register holds either a 32-bit primitive
// (v) or an object reference (o). Writers keep the invariant "exactly one of
// them is meaningful", which lets null checks and reference comparisons work
// on the same register file the bytecode uses (this is what the previous
// two-array design got wrong).
struct Reg {
    uint32_t v = 0;
    Object* o = nullptr;
};

// A call result / argument transported between interpreter and natives.
struct Value {
    uint64_t bits = 0;
    Object* ref = nullptr;

    static Value fromInt(int32_t i) { Value x; x.bits = static_cast<uint32_t>(i); return x; }
    static Value fromLong(int64_t l) { Value x; x.bits = static_cast<uint64_t>(l); return x; }
    static Value fromRef(Object* r) { Value x; x.ref = r; return x; }
    int32_t i32() const { return static_cast<int32_t>(static_cast<uint32_t>(bits)); }
    int64_t i64() const { return static_cast<int64_t>(bits); }
};

class Frame {
public:
    Frame(const DexLoader::CodeItem* codeItem, const RMethod* method,
          const DexLoader::DexFile* dex, uint32_t dexIndex)
        : codeItem(codeItem), method(method), dexFile(dex), dexIndex(dexIndex) {
        numRegs = codeItem ? codeItem->registersSize : 0;
        regs.reset(new Reg[numRegs ? numRegs : 1]);
    }

    std::unique_ptr<Reg[]> regs;
    uint32_t numRegs = 0;

    const DexLoader::CodeItem* codeItem = nullptr;
    const RMethod* method = nullptr;
    const DexLoader::DexFile* dexFile = nullptr;
    uint32_t dexIndex = 0;
    uint32_t pc = 0;
    Frame* previous = nullptr;

    // Result of the most recent invoke (consumed by move-result*).
    Value lastResult;

    // ---- register accessors (bounds are guaranteed by the verifier; we
    // still clamp so corrupt code cannot write out of the array) ----------
    uint32_t getRegister(uint32_t r) const { return r < numRegs ? regs[r].v : 0; }
    void setRegister(uint32_t r, uint32_t value) {
        if (r < numRegs) { regs[r].v = value; regs[r].o = nullptr; }
    }
    uint64_t getWideRegister(uint32_t r) const {
        return (static_cast<uint64_t>(getRegister(r + 1)) << 32) | getRegister(r);
    }
    void setWideRegister(uint32_t r, uint64_t value) {
        setRegister(r, static_cast<uint32_t>(value));
        setRegister(r + 1, static_cast<uint32_t>(value >> 32));
    }
    Object* getObjectRegister(uint32_t r) const { return r < numRegs ? regs[r].o : nullptr; }
    void setObjectRegister(uint32_t r, Object* obj) {
        if (r < numRegs) { regs[r].v = 0; regs[r].o = obj; }
    }
    bool isZero(uint32_t r) const {
        return r >= numRegs || (regs[r].o == nullptr && regs[r].v == 0);
    }
    void copyRegister(uint32_t dst, uint32_t src) {
        if (dst < numRegs) regs[dst] = (src < numRegs) ? regs[src] : Reg{};
    }
};

} // namespace NativeDex::Runtime::BytecodeInterpreter
