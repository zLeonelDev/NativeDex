#pragma once

#include "../../runtime/object_model/Object.h"

namespace NativeDex::Android::Content {

class Context : public Runtime::ObjectModel::Object {
public:
    Context() = default;
    virtual ~Context() = default;

    // Object interface implementation
    size_t objectSize() const override { return sizeof(Context); }
    void visitReferences(void (*visitor)(Runtime::ObjectModel::Object**)) override {}
};

} // namespace NativeDex::Android::Content
