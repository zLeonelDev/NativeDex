#pragma once

#include <vector>
#include <cstdint>
#include <string_view>
#include "BinaryReader.h"
#include "StringTable.h"

namespace NativeDex::Runtime::DexLoader {

class TypeTable {
public:
    void load(BinaryReader& reader, uint32_t offset, uint32_t count);
    
    uint32_t getTypeId(uint32_t index) const noexcept {
        return typeIds_[index];
    }

    std::string_view getTypeDescriptor(uint32_t index, const StringTable& strings) const {
        if (index >= typeIds_.size()) return {};
        return strings.getString(typeIds_[index]);
    }

    uint32_t size() const noexcept { return static_cast<uint32_t>(typeIds_.size()); }

private:
    std::vector<uint32_t> typeIds_;
};

} // namespace NativeDex::Runtime::DexLoader