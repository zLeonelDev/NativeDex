#include "BytecodeInterpreter.h"

#include <iostream>

namespace NativeDex::Runtime::BytecodeInterpreter {

BytecodeInterpreter::BytecodeInterpreter(std::vector<const DexLoader::DexFile*> dexFiles,
                                         Heap::HeapManager& /*heap*/, bool debug)
    : debug_(debug) {
    vm_ = std::make_unique<Vm>(std::move(dexFiles), debug);
}

BytecodeInterpreter::~BytecodeInterpreter() = default;

const DexLoader::DexFile* BytecodeInterpreter::findDexForClass(const std::string& descriptor) const {
    Vm::Locker lk(*vm_);
    RuntimeClass* c = vm_->findDexClass(descriptor);
    return c ? c->dex : nullptr;
}

Object* BytecodeInterpreter::newObject(const std::string& descriptor) {
    Object* result = nullptr;
    vm_->runGuarded([&] {
        RuntimeClass* c = vm_->findDexClass(descriptor);
        if (!c) c = vm_->findClass(descriptor);
        vm_->ensureInitialized(c);
        Object* o = vm_->newInstance(c);
        const RMethod* ctor = vm_->findMethod(c, "<init>", "()V");
        if (ctor) {
            vm_->invoke(ctor, o, {});
        } else {
            Target t = vm_->resolveVirtual(c, "<init>()V", "<init>", "()V");
            if (t.valid()) vm_->invokeTarget(t, nullptr, o, {});
        }
        result = o;
    }, "new " + descriptor);
    return result;
}

bool BytecodeInterpreter::callMethod(Object* self, const std::string& name, const std::string& proto,
                                     const std::vector<Value>& args, Value* result) {
    bool found = false;
    bool ok = vm_->runGuarded([&] {
        RuntimeClass* k = vm_->classOf(self);
        Target t = vm_->resolveVirtual(k, name + proto, name, proto);
        if (!t.valid()) return;
        found = true;
        Value v = vm_->invokeTarget(t, nullptr, self, args);
        if (result) *result = v;
    }, name + proto);
    return ok && found;
}

} // namespace NativeDex::Runtime::BytecodeInterpreter
