#include <iostream>
#include <cstdint>
#include "Parser.h"
#include "DexHeader.h"
#include "StringTable.h"
#include "TypeTable.h"
#include "ProtoTable.h"
#include "FieldTable.h"
#include "MethodTable.h"
#include "ClassTable.h"

namespace NativeDex::Runtime::DexLoader {

void Parser::parse(DexFile& dexFile) {
    size_t originalPos = reader_.tell();
    reader_.seek(0);

    // Parse header
    for (size_t i = 0; i < 8; ++i) {
        dexFile.header.magic[i] = reader_.readU8();
    }
    dexFile.header.checksum = reader_.readU32();
    for (size_t i = 0; i < 20; ++i) {
        dexFile.header.signature[i] = reader_.readU8();
    }
    dexFile.header.fileSize = reader_.readU32();
    dexFile.header.headerSize = reader_.readU32();
    dexFile.header.endianTag = reader_.readU32();
    dexFile.header.linkSize = reader_.readU32();
    dexFile.header.linkOff = reader_.readU32();
    dexFile.header.mapOff = reader_.readU32();
    dexFile.header.stringIdsSize = reader_.readU32();
    dexFile.header.stringIdsOff = reader_.readU32();
    dexFile.header.typeIdsSize = reader_.readU32();
    dexFile.header.typeIdsOff = reader_.readU32();
    dexFile.header.protoIdsSize = reader_.readU32();
    dexFile.header.protoIdsOff = reader_.readU32();
    dexFile.header.fieldIdsSize = reader_.readU32();
    dexFile.header.fieldIdsOff = reader_.readU32();
    dexFile.header.methodIdsSize = reader_.readU32();
    dexFile.header.methodIdsOff = reader_.readU32();
    dexFile.header.classDefsSize = reader_.readU32();
    dexFile.header.classDefsOff = reader_.readU32();
    dexFile.header.dataSize = reader_.readU32();
    dexFile.header.dataOff = reader_.readU32();

    if (!dexFile.header.isValid()) {
        throw std::runtime_error("Invalid DEX magic");
    }

    // Parse string table
    if (dexFile.header.stringIdsSize > 0) {
        dexFile.strings.load(reader_, dexFile.header.stringIdsOff, dexFile.header.stringIdsSize);
    }

    // Parse type table
    if (dexFile.header.typeIdsSize > 0) {
        dexFile.types.load(reader_, dexFile.header.typeIdsOff, dexFile.header.typeIdsSize);
    }

    // Parse proto table
    if (dexFile.header.protoIdsSize > 0) {
        dexFile.protos.load(reader_, dexFile.header.protoIdsOff, dexFile.header.protoIdsSize);
    }

    // Parse field table
    if (dexFile.header.fieldIdsSize > 0) {
        dexFile.fields.load(reader_, dexFile.header.fieldIdsOff, dexFile.header.fieldIdsSize);
    }

    // Parse method table
    if (dexFile.header.methodIdsSize > 0) {
        dexFile.methods.load(reader_, dexFile.header.methodIdsOff, dexFile.header.methodIdsSize);
    }

    // Parse class table
    if (dexFile.header.classDefsSize > 0) {
        dexFile.classes.load(reader_, dexFile.header.classDefsOff, dexFile.header.classDefsSize);
    }

    // Parse code items from class data
    if (dexFile.header.classDefsSize > 0) {
        dexFile.codeItems.resize(dexFile.methods.size());
        
        for (uint32_t classIdx = 0; classIdx < dexFile.classes.size(); ++classIdx) {
            const auto& classDef = dexFile.classes.getClassDef(classIdx);
            if (classDef.classDataOff == 0) continue;

            reader_.seek(classDef.classDataOff);
            
            uint32_t staticFieldsSize = reader_.readUleb128();
            uint32_t instanceFieldsSize = reader_.readUleb128();
            uint32_t directMethodsSize = reader_.readUleb128();
            uint32_t virtualMethodsSize = reader_.readUleb128();

            // Skip static fields
            for (uint32_t i = 0; i < staticFieldsSize; ++i) {
                reader_.readUleb128(); // field_idx_diff
                reader_.readUleb128(); // access_flags
            }
            // Skip instance fields
            for (uint32_t i = 0; i < instanceFieldsSize; ++i) {
                reader_.readUleb128(); // field_idx_diff
                reader_.readUleb128(); // access_flags
            }

            auto readMethodList = [&](uint32_t methodCount, bool isDirect) {
                uint32_t lastMethodIdx = 0;
                for (uint32_t i = 0; i < methodCount; ++i) {
                    uint32_t methodIdxDiff = reader_.readUleb128();
                    uint32_t accessFlags = reader_.readUleb128();
                    uint32_t codeOff = reader_.readUleb128();
                    uint32_t methodIdx = lastMethodIdx + methodIdxDiff;
                    lastMethodIdx = methodIdx;

                    if (codeOff == 0) continue; // native/abstract

                    // Parse CodeItem
                    size_t savedPos = reader_.tell();
                    reader_.seek(codeOff);
                    
                    CodeItem codeItem{};
                    codeItem.registersSize = reader_.readU16();
                    codeItem.insSize = reader_.readU16();
                    codeItem.outsSize = reader_.readU16();
                    codeItem.triesSize = reader_.readU16();
                    codeItem.debugInfoOff = reader_.readU32();
                    codeItem.insnsSize = reader_.readU32();
                    codeItem.insns.resize(codeItem.insnsSize);
                    if (codeItem.insnsSize > 0) {
                        reader_.readBytes(reinterpret_cast<uint8_t*>(codeItem.insns.data()), codeItem.insnsSize * 2);
                    }

                    // DEX alignment: if insnsSize is odd, skip 2 padding bytes
                    // before try_items to reach 4-byte alignment.
                    if (codeItem.triesSize > 0 && (codeItem.insnsSize & 1)) {
                        reader_.readU16(); // skip padding
                    }

                    // Parse try items and catch handlers (if any).
                    // Wrapped in try-catch: if the data is corrupt, skip gracefully.
                    if (codeItem.triesSize > 0) {
                        size_t tryStartPos = reader_.tell();
                        try {
                            codeItem.tryItems.resize(codeItem.triesSize);
                            for (uint16_t t = 0; t < codeItem.triesSize; ++t) {
                                codeItem.tryItems[t].startAddr = reader_.readU32();
                                codeItem.tryItems[t].insnCount = reader_.readU16();
                                codeItem.tryItems[t].handlerOff = reader_.readU16();
                            }
                            // Read encoded_catch_handler_list.
                            size_t handlerDataStart = reader_.tell();
                            uint32_t handlerCount = reader_.readUleb128();
                            codeItem.catchHandlers.resize(handlerCount);
                            std::vector<size_t> handlerByteOffsets(handlerCount);
                            for (uint32_t h = 0; h < handlerCount; ++h) {
                                handlerByteOffsets[h] = reader_.tell() - handlerDataStart;
                                int32_t ssize = reader_.readSleb128();
                                auto& ch = codeItem.catchHandlers[h];
                                if (ssize > 0) {
                                    ch.size = static_cast<uint32_t>(ssize);
                                    ch.handlers.resize(ch.size);
                                    for (uint32_t hi = 0; hi < ch.size; ++hi) {
                                        ch.handlers[hi].typeIdx = reader_.readUleb128();
                                        ch.handlers[hi].address = reader_.readUleb128();
                                    }
                                    ch.hasCatchAll = false;
                                } else if (ssize < 0) {
                                    ch.hasCatchAll = true;
                                    ch.size = static_cast<uint32_t>(-ssize);
                                    ch.handlers.resize(ch.size);
                                    for (uint32_t hi = 0; hi < ch.size; ++hi) {
                                        ch.handlers[hi].typeIdx = reader_.readUleb128();
                                        ch.handlers[hi].address = reader_.readUleb128();
                                    }
                                    ch.catchAllHandler = reader_.readUleb128();
                                } else {
                                    ch.size = 0;
                                    ch.hasCatchAll = true;
                                    ch.catchAllHandler = reader_.readUleb128();
                                }
                            }
                            // Convert handlerOff byte offsets to handler indices.
                            for (uint16_t t = 0; t < codeItem.triesSize; ++t) {
                                uint32_t byteOff = codeItem.tryItems[t].handlerOff;
                                codeItem.tryItems[t].handlerOff = 0;
                                for (uint32_t h = 0; h < handlerCount; ++h) {
                                    if (handlerByteOffsets[h] == byteOff) {
                                        codeItem.tryItems[t].handlerOff = static_cast<uint16_t>(h);
                                        break;
                                    }
                                }
                            }
                        } catch (...) {
                            // Corrupt try/catch data — skip it.
                            codeItem.tryItems.clear();
                            codeItem.catchHandlers.clear();
                            codeItem.triesSize = 0;
                            reader_.seek(tryStartPos);
                            // Skip past try_items and handler list.
                            // Since we can't parse, just bail.
                        }
                    }
                    
                    // Store in maps
                    uint64_t methodKey = DexFile::makeMethodKey(classIdx, methodIdx);
                    dexFile.methodCodeOffsetMap[methodKey] = codeOff;
                    dexFile.codeItemsByOffset[codeOff] = codeItem;

                    // Store in the per-method-index array used by the interpreter
                    if (methodIdx < dexFile.codeItems.size()) {
                        dexFile.codeItems[methodIdx] = codeItem;
                    }
                    
                    reader_.seek(savedPos);
                }
            };

            // Parse direct methods
            readMethodList(directMethodsSize, true);
            // Parse virtual methods
            readMethodList(virtualMethodsSize, false);
        }
    }

    reader_.seek(originalPos);
}

} // namespace NativeDex::Runtime::DexLoader
