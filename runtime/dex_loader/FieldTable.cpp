#include "FieldTable.h"
#include "BinaryReader.h"

namespace NativeDex::Runtime::DexLoader {

void FieldTable::load(BinaryReader& reader, uint32_t offset, uint32_t count) {
    size_t originalPos = reader.tell();
    reader.seek(offset);
    fields_.clear();
    fields_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        FieldIdItem item{};
        item.classIdx = reader.readU16();
        item.typeIdx = reader.readU16();
        item.nameIdx = reader.readU32();
        fields_.push_back(item);
    }
    reader.seek(originalPos);
}

} // namespace NativeDex::Runtime::DexLoader