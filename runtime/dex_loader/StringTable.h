#pragma once

#include <vector>
#include <cstdint>
#include <string_view>
#include <string>
#include <cstddef>
#include "BinaryReader.h"

namespace NativeDex::Runtime::DexLoader {

class StringTable {
public:
    void load(BinaryReader& reader, uint32_t offset, uint32_t count);

    std::string_view getString(uint32_t index) const noexcept {
        if (index >= strings_.size()) return {};
        return std::string_view(strings_[index]);
    }

    uint32_t size() const noexcept { return static_cast<uint32_t>(strings_.size()); }

private:
    // Each string owns its own buffer, so string_views handed out by getString
    // stay valid even if the StringTable (or its owning DexFile) is copied or
    // moved. A shared contiguous buffer would invalidate views on copy.
    std::vector<std::string> strings_;

    static std::string decodeMutf8(const std::string_view& mutf8);
};

} // namespace NativeDex::Runtime::DexLoader