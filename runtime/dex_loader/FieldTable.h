#pragma once

#include <vector>
#include <cstdint>
#include <string_view>
#include "BinaryReader.h"
#include "StringTable.h"
#include "TypeTable.h"

namespace NativeDex::Runtime::DexLoader {

struct FieldIdItem {
    uint16_t classIdx;
    uint16_t typeIdx;
    uint32_t nameIdx;
};

class FieldTable {
public:
    void load(BinaryReader& reader, uint32_t offset, uint32_t count);
    
    const FieldIdItem& getField(uint32_t index) const noexcept {
        return fields_[index];
    }

    std::string_view getFieldName(uint32_t index, const StringTable& strings) const {
        if (index >= fields_.size()) return {};
        return strings.getString(fields_[index].nameIdx);
    }

    std::string_view getFieldType(uint32_t index, const TypeTable& types, const StringTable& strings) const {
        if (index >= fields_.size()) return {};
        return types.getTypeDescriptor(fields_[index].typeIdx, strings);
    }

    std::string_view getFieldClass(uint32_t index, const TypeTable& types, const StringTable& strings) const {
        if (index >= fields_.size()) return {};
        return types.getTypeDescriptor(fields_[index].classIdx, strings);
    }

    uint32_t size() const noexcept { return static_cast<uint32_t>(fields_.size()); }

private:
    std::vector<FieldIdItem> fields_;
};

} // namespace NativeDex::Runtime::DexLoader