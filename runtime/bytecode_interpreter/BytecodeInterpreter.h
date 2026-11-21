#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../dex_loader/DexFile.h"
#include "../heap/HeapManager.h"
#include "Vm.h"

namespace NativeDex::Runtime::BytecodeInterpreter {

// Thin facade over Vm used by the launcher.
class BytecodeInterpreter {
public:
    BytecodeInterpreter(std::vector<const DexLoader::DexFile*> dexFiles,
                        Heap::HeapManager& heap, bool debug = false);
    ~BytecodeInterpreter();

    Vm& vm() { return *vm_; }
    bool isDebug() const { return debug_; }

    // The DEX that defines `descriptor` (e.g. "Lcom/foo/Main;"), or null.
    const DexLoader::DexFile* findDexForClass(const std::string& descriptor) const;

    // Allocates an instance of `descriptor` and runs its no-argument
    // constructor. Returns null if the class does not exist or construction
    // threw.
    Object* newObject(const std::string& descriptor);

    // Virtual call on `self` guarded against uncaught Java exceptions.
    // Returns false when the method does not exist or an exception escaped.
    bool callMethod(Object* self, const std::string& name, const std::string& proto,
                    const std::vector<Value>& args = {}, Value* result = nullptr);

private:
    std::unique_ptr<Vm> vm_;
    bool debug_ = false;
};

} // namespace NativeDex::Runtime::BytecodeInterpreter
