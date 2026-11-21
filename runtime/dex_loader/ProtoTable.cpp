#include "ProtoTable.h"
#include "BinaryReader.h"

namespace NativeDex::Runtime::DexLoader {

void ProtoTable::load(BinaryReader& reader, uint32_t offset, uint32_t count) {
    size_t originalPos = reader.tell();
    reader.seek(offset);
    protos_.clear();
    protos_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        ProtoIdItem item{};
        item.shortyIdx = reader.readU32();
        item.returnTypeIdx = reader.readU32();
        item.parametersOff = reader.readU32();
        protos_.push_back(item);
    }
    reader.seek(originalPos);
}

std::vector<std::string> ProtoTable::getParamTypeDescriptors(uint32_t index,
                                                            const TypeTable& types,
                                                            const StringTable& strings,
                                                            BinaryReader& reader) const {
    std::vector<std::string> out;
    if (index >= protos_.size()) return out;
    uint32_t off = protos_[index].parametersOff;
    if (off == 0) return out;
    size_t original = reader.tell();
    reader.seek(off);
    uint32_t paramCount = reader.readU32();
    out.reserve(paramCount);
    for (uint32_t i = 0; i < paramCount; ++i) {
        uint32_t typeIdx = reader.readU16();
        if (typeIdx < types.size()) {
            out.emplace_back(types.getTypeDescriptor(typeIdx, strings));
        }
    }
    reader.seek(original);
    return out;
}

std::string ProtoTable::getReturnTypeDescriptor(uint32_t index,
                                                const TypeTable& types,
                                                const StringTable& strings) const {
    if (index >= protos_.size()) return {};
    uint32_t typeIdx = protos_[index].returnTypeIdx;
    if (typeIdx >= types.size()) return {};
    return std::string(types.getTypeDescriptor(typeIdx, strings));
}

} // namespace NativeDex::Runtime::DexLoader