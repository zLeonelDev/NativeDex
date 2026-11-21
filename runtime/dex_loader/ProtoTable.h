#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <string_view>
#include "BinaryReader.h"
#include "StringTable.h"
#include "TypeTable.h"

namespace NativeDex::Runtime::DexLoader {

struct ProtoIdItem {
    uint32_t shortyIdx;    // string index
    uint32_t returnTypeIdx; // type index
    uint32_t parametersOff; // offset to type list
};

class ProtoTable {
public:
    void load(BinaryReader& reader, uint32_t offset, uint32_t count);

    const ProtoIdItem& getProto(uint32_t index) const noexcept {
        return protos_[index];
    }

    std::string_view getShorty(uint32_t index, const StringTable& strings) const {
        if (index >= protos_.size()) return {};
        return strings.getString(protos_[index].shortyIdx);
    }

    // Read the parameter type list at parametersOff and return the type
    // descriptors as a vector. Returns empty on parse failure.
    std::vector<std::string> getParamTypeDescriptors(uint32_t index,
                                                    const TypeTable& types,
                                                    const StringTable& strings,
                                                    BinaryReader& reader) const;

    // Get the return type descriptor for a given proto index.
    std::string getReturnTypeDescriptor(uint32_t index,
                                        const TypeTable& types,
                                        const StringTable& strings) const;

    uint32_t size() const noexcept { return static_cast<uint32_t>(protos_.size()); }

private:
    std::vector<ProtoIdItem> protos_;
};

} // namespace NativeDex::Runtime::DexLoader