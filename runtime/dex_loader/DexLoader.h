#pragma once

#include <string>
#include <vector>
#include <memory>
#include "DexFile.h"
#include "Parser.h"
#include "BinaryReader.h"

namespace NativeDex::Runtime::DexLoader {

class DexLoader {
public:
    DexLoader() = default;
    ~DexLoader() = default;

    // Load DEX file from path
    bool loadFromFile(const std::string& path);
    
    // Load DEX from memory buffer
    bool loadFromMemory(const std::vector<uint8_t>& data);
    bool loadFromMemory(const uint8_t* data, size_t size);

    // Get parsed DEX file
    const DexFile& getDexFile() const noexcept { return *dexFile_; }
    DexFile& getDexFile() noexcept { return *dexFile_; }
    bool hasDexFile() const noexcept { return dexFile_ != nullptr; }

private:
    std::unique_ptr<DexFile> dexFile_;
};

} // namespace NativeDex::Runtime::DexLoader