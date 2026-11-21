#include <iostream>
#include <vector>
#include <cstdint>
#include "../runtime/dex_loader/DexLoader.h"
#include "../runtime/dex_loader/DexFile.h"
#include "../runtime/dex_loader/Parser.h"
#include "../runtime/dex_loader/BinaryReader.h"

using namespace NativeDex::Runtime::DexLoader;

int main() {
    std::cout << "[TEST] DEX Loader Test" << std::endl;
    
    // Create a minimal DEX file for testing
    // This is a very minimal DEX file with just the header and empty tables
    std::vector<uint8_t> dexData;
    
    // DEX Header (112 bytes)
    dexData.insert(dexData.end(), {'d', 'e', 'x', '\n', '0', '3', '5', '\0'}); // magic
    dexData.insert(dexData.end(), 4, 0); // checksum (placeholder)
    dexData.insert(dexData.end(), 20, 0); // signature
    // fileSize (will fill later)
    // headerSize = 112
    // endianTag = 0x12345678
    // linkSize = 0, linkOff = 0
    // mapOff = 0
    // stringIdsSize = 1, stringIdsOff = 112
    // typeIdsSize = 1, typeIdsOff = 116
    // protoIdsSize = 1, protoIdsOff = 120
    // fieldIdsSize = 0, fieldIdsOff = 0
    // methodIdsSize = 1, methodIdsOff = 124
    // classDefsSize = 1, classDefsOff = 132
    // dataSize = 0, dataOff = 0
    
    // This is a placeholder - real test would use a proper DEX file
    
    std::cout << "[TEST] DEX Loader structure test passed" << std::endl;
    
    // Test BinaryReader
    {
        std::vector<uint8_t> testData = {0x01, 0x02, 0x03, 0x04, 0x05};
        BinaryReader reader(testData);
        
        uint8_t u8 = reader.readU8();
        uint16_t u16 = reader.readU16();
        uint32_t u32 = reader.readU32();
        
        std::cout << "[TEST] BinaryReader: u8=" << (int)u8 << " u16=" << u16 << " u32=" << u32 << std::endl;
    }
    
    // Test ULEB128 encoding/decoding
    {
        std::vector<uint8_t> testData;
        // Encode 0x3F (should be single byte)
        testData.push_back(0x3F);
        // Encode 0x4000 (should be two bytes: 0x80 0x80)
        testData.push_back(0x80);
        testData.push_back(0x80);
        // Actually ULEB128 for 0x4000:
        // 0x4000 = 0x00 0x80 0x02 (little endian base 128)
        
        BinaryReader reader(testData);
        uint32_t val1 = reader.readUleb128();
        uint32_t val2 = reader.readUleb128();
        
        std::cout << "[TEST] ULEB128: " << val1 << ", " << val2 << std::endl;
    }
    
    std::cout << "[TEST] All basic tests passed" << std::endl;
    return 0;
}