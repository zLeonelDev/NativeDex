#include "MethodTable.h"
#include "BinaryReader.h"

namespace NativeDex::Runtime::DexLoader {

void MethodTable::load(BinaryReader& reader, uint32_t offset, uint32_t count) {
    size_t originalPos = reader.tell();
    reader.seek(offset);
    methods_.clear();
    methods_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        MethodIdItem item{};
        item.classIdx = reader.readU16();
        item.protoIdx = reader.readU16();
        item.nameIdx = reader.readU32();
        methods_.push_back(item);
    }
    reader.seek(originalPos);
}

} // namespace NativeDex::Runtime::DexLoader