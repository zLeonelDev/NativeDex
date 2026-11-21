#pragma once

#include "Object.h"

namespace NativeDex::Runtime::ObjectModel {

// Concrete heap object used for every ordinary Java instance (DEX-defined
// classes and framework objects alike). Field storage lives in the base
// class (fieldSlots) and native state in Object::native / nativeHandle.
class InstanceObject final : public Object {
public:
    InstanceObject() = default;
    size_t objectSize() const override {
        return sizeof(InstanceObject) + fieldSlots.size() * sizeof(FieldSlot);
    }
    void visitReferences(void (*visitor)(Object**)) override {
        for (auto& s : fieldSlots) {
            if (s.ref) visitor(&s.ref);
        }
    }
};

} // namespace NativeDex::Runtime::ObjectModel
