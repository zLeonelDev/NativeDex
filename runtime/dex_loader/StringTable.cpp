#include "StringTable.h"
#include "BinaryReader.h"
#include <iostream>

namespace NativeDex::Runtime::DexLoader {

std::string StringTable::decodeMutf8(const std::string_view& mutf8) {
    std::string result;
    result.reserve(mutf8.size());
    for (size_t i = 0; i < mutf8.size(); ++i) {
        uint8_t b = static_cast<uint8_t>(mutf8[i]);
        if (b == 0) break;
        if (b == 0xC0 && i + 1 < mutf8.size() && static_cast<uint8_t>(mutf8[i + 1]) == 0x80) {
            result.push_back('\0');
            ++i;
        } else if ((b & 0x80) == 0) {
            result.push_back(static_cast<char>(b));
        } else if ((b & 0xE0) == 0xC0) {
            if (i + 1 >= mutf8.size()) break;
            result.append(mutf8.substr(i, 2));
            ++i;
        } else if ((b & 0xF0) == 0xE0) {
            if (i + 2 >= mutf8.size()) break;
            result.append(mutf8.substr(i, 3));
            i += 2;
        } else if ((b & 0xF8) == 0xF0) {
            if (i + 3 >= mutf8.size()) break;
            result.append(mutf8.substr(i, 4));
            i += 3;
        } else {
            result.push_back(static_cast<char>(b));
        }
    }
    return result;
}

void StringTable::load(BinaryReader& reader, uint32_t offset, uint32_t count) {
    size_t originalPos = reader.tell();
    reader.seek(offset);

    std::vector<uint32_t> stringOffsets(count);
    for (uint32_t i = 0; i < count; ++i) {
        stringOffsets[i] = reader.readU32();
    }

    strings_.clear();
    strings_.reserve(count);
    for (uint32_t off : stringOffsets) {
        reader.seek(off);
        reader.readUleb128(); // utf16 code unit count (informational)
        std::string_view raw = reader.readZeroTerminatedString();
        strings_.push_back(decodeMutf8(raw));
    }

    reader.seek(originalPos);
}

} // namespace NativeDex::Runtime::DexLoader