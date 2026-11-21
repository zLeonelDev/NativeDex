#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace NativeDex::Runtime::DexLoader {

struct DexHeader {
    std::array<char, 8> magic{};
    uint32_t checksum = 0;
    std::array<uint8_t, 20> signature{};
    uint32_t fileSize = 0;
    uint32_t headerSize = 0;
    uint32_t endianTag = 0;
    uint32_t linkSize = 0;
    uint32_t linkOff = 0;
    uint32_t mapOff = 0;
    uint32_t stringIdsSize = 0;
    uint32_t stringIdsOff = 0;
    uint32_t typeIdsSize = 0;
    uint32_t typeIdsOff = 0;
    uint32_t protoIdsSize = 0;
    uint32_t protoIdsOff = 0;
    uint32_t fieldIdsSize = 0;
    uint32_t fieldIdsOff = 0;
    uint32_t methodIdsSize = 0;
    uint32_t methodIdsOff = 0;
    uint32_t classDefsSize = 0;
    uint32_t classDefsOff = 0;
    uint32_t dataSize = 0;
    uint32_t dataOff = 0;

    std::string_view magicString() const noexcept {
        return std::string_view(magic.data(), 8);
    }

    bool isValid() const noexcept {
        return magic[0] == 'd' && magic[1] == 'e' && magic[2] == 'x' && magic[3] == '\n' &&
               magic[4] >= '0' && magic[4] <= '9' &&
               magic[5] >= '0' && magic[5] <= '9' &&
               magic[6] >= '0' && magic[6] <= '9' && magic[7] == '\0';
    }

    int version() const noexcept {
        if (!isValid()) return -1;
        return (magic[4] - '0') * 100 + (magic[5] - '0') * 10 + (magic[6] - '0');
    }
};

} // namespace NativeDex::Runtime::DexLoader