#include "ClassTable.h"
#include "BinaryReader.h"

namespace NativeDex::Runtime::DexLoader {

void ClassTable::load(BinaryReader& reader, uint32_t offset, uint32_t count) {
    size_t originalPos = reader.tell();
    reader.seek(offset);
    classDefs_.clear();
    classDefs_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        ClassDefItem item{};
        item.classIdx = reader.readU32();
        item.accessFlags = reader.readU32();
        item.superclassIdx = reader.readU32();
        item.interfacesOff = reader.readU32();
        item.sourceFileIdx = reader.readU32();
        item.annotationsOff = reader.readU32();
        item.classDataOff = reader.readU32();
        item.staticValuesOff = reader.readU32();
        classDefs_.push_back(item);
    }
    reader.seek(originalPos);
}

const ClassDefItem& ClassTable::getClassDef(uint32_t index) const {
    if (index >= classDefs_.size()) {
        throw std::out_of_range("ClassTable: index out of range");
    }
    return classDefs_[index];
}

ClassDataItem ClassTable::parseClassData(BinaryReader& reader, uint32_t offset) const {
    ClassDataItem result;
    size_t originalPos = reader.tell();
    reader.seek(offset);
    
    uint32_t staticFieldsSize = reader.readUleb128();
    uint32_t instanceFieldsSize = reader.readUleb128();
    uint32_t directMethodsSize = reader.readUleb128();
    uint32_t virtualMethodsSize = reader.readUleb128();
    
    // Parse static fields
    result.staticFields.reserve(staticFieldsSize);
    uint32_t lastFieldIdx = 0;
    for (uint32_t i = 0; i < staticFieldsSize; ++i) {
        uint32_t fieldIdxDiff = reader.readUleb128();
        uint32_t accessFlags = reader.readUleb128();
        lastFieldIdx += fieldIdxDiff;
        result.staticFields.push_back(lastFieldIdx);
    }
    
    // Parse instance fields
    result.instanceFields.reserve(instanceFieldsSize);
    lastFieldIdx = 0;
    for (uint32_t i = 0; i < instanceFieldsSize; ++i) {
        uint32_t fieldIdxDiff = reader.readUleb128();
        uint32_t accessFlags = reader.readUleb128();
        lastFieldIdx += fieldIdxDiff;
        result.instanceFields.push_back(lastFieldIdx);
    }
    
    // Parse direct methods
    result.directMethods.reserve(directMethodsSize);
    uint32_t lastMethodIdx = 0;
    for (uint32_t i = 0; i < directMethodsSize; ++i) {
        uint32_t methodIdxDiff = reader.readUleb128();
        uint32_t accessFlags = reader.readUleb128();
        uint32_t codeOff = reader.readUleb128();
        lastMethodIdx += methodIdxDiff;
        result.directMethods.push_back(lastMethodIdx);
    }
    
    // Parse virtual methods
    result.virtualMethods.reserve(virtualMethodsSize);
    lastMethodIdx = 0;
    for (uint32_t i = 0; i < virtualMethodsSize; ++i) {
        uint32_t methodIdxDiff = reader.readUleb128();
        uint32_t accessFlags = reader.readUleb128();
        uint32_t codeOff = reader.readUleb128();
        lastMethodIdx += methodIdxDiff;
        result.virtualMethods.push_back(lastMethodIdx);
    }
    
    reader.seek(originalPos);
    return result;
}

CodeItem ClassTable::parseCodeItem(BinaryReader& reader, uint32_t offset) const {
    CodeItem result;
    size_t originalPos = reader.tell();
    reader.seek(offset);
    
    result.registersSize = reader.readU16();
    result.insSize = reader.readU16();
    result.outsSize = reader.readU16();
    result.triesSize = reader.readU16();
    result.debugInfoOff = reader.readU32();
    result.insnsSize = reader.readU32();
    result.insns.resize(result.insnsSize);
    for (uint32_t i = 0; i < result.insnsSize; ++i) {
        result.insns[i] = reader.readU16();
    }
    
    // DEX alignment: if insnsSize is odd, skip 2 padding bytes.
    if (result.triesSize > 0 && (result.insnsSize & 1)) {
        reader.readU16(); // skip padding
    }

    // Parse try items
    if (result.triesSize > 0) {
        result.tryItems.resize(result.triesSize);
        for (uint16_t i = 0; i < result.triesSize; ++i) {
            result.tryItems[i].startAddr = reader.readU32();
            result.tryItems[i].insnCount = reader.readU16();
            result.tryItems[i].handlerOff = reader.readU16();
        }
    }
    
    reader.seek(originalPos);
    return result;
}

std::vector<TryItem> ClassTable::parseTryItems(BinaryReader& reader, uint32_t offset, uint16_t triesSize) const {
    std::vector<TryItem> result;
    result.reserve(triesSize);
    size_t originalPos = reader.tell();
    reader.seek(offset);
    
    for (uint16_t i = 0; i < triesSize; ++i) {
        TryItem item{};
        item.startAddr = reader.readU32();
        item.insnCount = reader.readU16();
        item.handlerOff = reader.readU16();
        result.push_back(item);
    }
    
    reader.seek(originalPos);
    return result;
}

std::vector<EncodedCatchHandler> ClassTable::parseCatchHandlers(BinaryReader& reader, uint32_t offset) const {
    std::vector<EncodedCatchHandler> result;
    size_t originalPos = reader.tell();
    reader.seek(offset);
    
    uint32_t handlersSize = reader.readUleb128();
    for (uint32_t i = 0; i < handlersSize; ++i) {
        EncodedCatchHandler handler{};
        handler.size = reader.readUleb128();
        handler.handlers.reserve(handler.size);
for (uint32_t j = 0; j < handler.size; ++j) {
            EncodedCatchHandlerItem item{};
            item.typeIdx = reader.readUleb128();
            item.address = reader.readUleb128();
            handler.handlers.push_back(item);
        }
        handler.catchAllHandler = reader.readUleb128();
        result.push_back(handler);
    }
    
    reader.seek(originalPos);
    return result;
}

} // namespace NativeDex::Runtime::DexLoader