#pragma once

#include <vector>
#include <cstdint>
#include <string_view>
#include "BinaryReader.h"
#include "StringTable.h"
#include "TypeTable.h"
#include "ProtoTable.h"

namespace NativeDex::Runtime::DexLoader {

struct MethodIdItem {
    uint16_t classIdx;
    uint16_t protoIdx;
    uint32_t nameIdx;
};

class MethodTable {
public:
    void load(BinaryReader& reader, uint32_t offset, uint32_t count);
    
    const MethodIdItem& getMethod(uint32_t index) const noexcept {
        return methods_[index];
    }

    std::string_view getMethodName(uint32_t index, const StringTable& strings) const {
        if (index >= methods_.size()) return {};
        return strings.getString(methods_[index].nameIdx);
    }

    std::string_view getMethodClass(uint32_t index, const TypeTable& types, const StringTable& strings) const {
        if (index >= methods_.size()) return {};
        return types.getTypeDescriptor(methods_[index].classIdx, strings);
    }

    const ProtoIdItem& getMethodProto(uint32_t index, const ProtoTable& protos) const {
        return protos.getProto(methods_[index].protoIdx);
    }

    uint32_t size() const noexcept { return static_cast<uint32_t>(methods_.size()); }

private:
    std::vector<MethodIdItem> methods_;
};

} // namespace NativeDex::Runtime::DexLoader