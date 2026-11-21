#include "TypeTable.h"
#include "BinaryReader.h"

namespace NativeDex::Runtime::DexLoader {

void TypeTable::load(BinaryReader& reader, uint32_t offset, uint32_t count) {
    size_t originalPos = reader.tell();
    reader.seek(offset);
    typeIds_.clear();
    typeIds_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        typeIds_.push_back(reader.readU32());
    }
    reader.seek(originalPos);
}

} // namespace NativeDex::Runtime::DexLoader