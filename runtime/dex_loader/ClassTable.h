#pragma once

#include <vector>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include "BinaryReader.h"
#include "StringTable.h"
#include "TypeTable.h"
#include "ProtoTable.h"
#include "FieldTable.h"
#include "MethodTable.h"

namespace NativeDex::Runtime::DexLoader {

struct ClassDefItem {
    uint32_t classIdx;
    uint32_t accessFlags;
    uint32_t superclassIdx;
    uint32_t interfacesOff;
    uint32_t sourceFileIdx;
    uint32_t annotationsOff;
    uint32_t classDataOff;
    uint32_t staticValuesOff;
};

struct ClassDataItem {
    std::vector<uint32_t> staticFields;
    std::vector<uint32_t> instanceFields;
    std::vector<uint32_t> directMethods;
    std::vector<uint32_t> virtualMethods;
};

struct TryItem {
    uint32_t startAddr;
    uint16_t insnCount;
    uint16_t handlerOff;
};

struct EncodedCatchHandlerItem {
    uint32_t typeIdx = 0;
    uint32_t address = 0;
};

struct EncodedCatchHandler {
    uint32_t size = 0;
    std::vector<EncodedCatchHandlerItem> handlers;
    uint32_t catchAllHandler = 0;
    bool hasCatchAll = false;
};

struct CodeItem {
    uint16_t registersSize;
    uint16_t insSize;
    uint16_t outsSize;
    uint16_t triesSize;
    uint32_t debugInfoOff;
    uint32_t insnsSize;
    std::vector<uint16_t> insns;
    std::vector<TryItem> tryItems;
    std::vector<EncodedCatchHandler> catchHandlers;
};

class ClassTable {
public:
    void load(BinaryReader& reader, uint32_t offset, uint32_t count);
    const ClassDefItem& getClassDef(uint32_t index) const;
    uint32_t size() const noexcept { return static_cast<uint32_t>(classDefs_.size()); }

    // Parse class data at given offset
    ClassDataItem parseClassData(BinaryReader& reader, uint32_t offset) const;
    
    // Parse CodeItem at given offset
    CodeItem parseCodeItem(BinaryReader& reader, uint32_t offset) const;
    
    // Parse try/catch handlers
    std::vector<TryItem> parseTryItems(BinaryReader& reader, uint32_t offset, uint16_t triesSize) const;
    std::vector<EncodedCatchHandler> parseCatchHandlers(BinaryReader& reader, uint32_t offset) const;

    std::string_view getClassDescriptor(uint32_t index, const TypeTable& types, const StringTable& strings) const {
        if (index >= classDefs_.size()) return {};
        return types.getTypeDescriptor(classDefs_[index].classIdx, strings);
    }

    std::string_view getSuperClassDescriptor(uint32_t index, const TypeTable& types, const StringTable& strings) const {
        if (index >= classDefs_.size()) return {};
        return types.getTypeDescriptor(classDefs_[index].superclassIdx, strings);
    }

private:
    std::vector<ClassDefItem> classDefs_;
};

} // namespace NativeDex::Runtime::DexLoader