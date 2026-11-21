#pragma once

#include "Rect.h"
#include "../../runtime/object_model/Object.h"

namespace NativeDex::Android::Graphics {

class Canvas : public Runtime::ObjectModel::Object {
public:
    Canvas() = default;
    virtual ~Canvas() = default;

    // Object interface implementation
    size_t objectSize() const override { return sizeof(Canvas); }
    void visitReferences(void (*visitor)(Runtime::ObjectModel::Object**)) override {}
};

} // namespace NativeDex::Android::Graphics
