#include "DexLoader.h"
#include <fstream>
#include <iostream>

namespace NativeDex::Runtime::DexLoader {

bool DexLoader::loadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "[DexLoader] Failed to open file: " << path << std::endl;
        return false;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::cerr << "[DexLoader] Failed to read file: " << path << std::endl;
        return false;
    }
    
    return loadFromMemory(buffer);
}

bool DexLoader::loadFromMemory(const std::vector<uint8_t>& data) {
    return loadFromMemory(data.data(), data.size());
}

bool DexLoader::loadFromMemory(const uint8_t* data, size_t size) {
    try {
        BinaryReader reader(data, size);
        dexFile_ = std::make_unique<DexFile>();
        dexFile_->rawData.assign(data, data + size);
        Parser parser(reader);
        parser.parse(*dexFile_);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[DexLoader] Parse error: " << e.what() << std::endl;
        dexFile_.reset();
        return false;
    }
}

} // namespace NativeDex::Runtime::DexLoader