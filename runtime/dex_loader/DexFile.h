#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include "DexHeader.h"
#include "StringTable.h"
#include "TypeTable.h"
#include "ProtoTable.h"
#include "FieldTable.h"
#include "MethodTable.h"
#include "ClassTable.h"

namespace NativeDex::Runtime::DexLoader {

// Use types from ClassTable.h (CodeItem, TryItem, EncodedCatchHandler, EncodedCatchHandlerItem)
// Use types from MethodTable.h (MethodIdItem)
// Use types from FieldTable.h (FieldIdItem)
// Use types from ProtoTable.h (ProtoIdItem)
// Use types from ClassTable.h (ClassDefItem)

class DexFile {
public:
    DexHeader header;
    StringTable strings;
    TypeTable types;
    ProtoTable protos;
    FieldTable fields;
    MethodTable methods;
    ClassTable classes;

    // Raw DEX bytes kept so ProtoTable can read parameter lists on demand.
    std::vector<uint8_t> rawData;

    // Code items by method index
    std::vector<CodeItem> codeItems;
    
    // Map from (classIdx, methodIdx) to code offset
    std::unordered_map<uint64_t, uint32_t> methodCodeOffsetMap;
    
    // Map from code offset to CodeItem
    std::unordered_map<uint32_t, CodeItem> codeItemsByOffset;
    
    // Get method name
    std::string_view getMethodName(uint32_t methodIdx) const {
        if (methodIdx >= methods.size()) return {};
        return strings.getString(methods.getMethod(methodIdx).nameIdx);
    }
    
    // Get method class descriptor
    std::string_view getMethodClass(uint32_t methodIdx) const {
        if (methodIdx >= methods.size()) return {};
        return types.getTypeDescriptor(methods.getMethod(methodIdx).classIdx, strings);
    }
    
    // Get field name
    std::string_view getFieldName(uint32_t fieldIdx) const {
        if (fieldIdx >= fields.size()) return {};
        return strings.getString(fields.getField(fieldIdx).nameIdx);
    }
    
    // Get field class descriptor
    std::string_view getFieldClass(uint32_t fieldIdx) const {
        if (fieldIdx >= fields.size()) return {};
        return types.getTypeDescriptor(fields.getField(fieldIdx).classIdx, strings);
    }
    
    // Get field type descriptor
    std::string_view getFieldType(uint32_t fieldIdx) const {
        if (fieldIdx >= fields.size()) return {};
        return types.getTypeDescriptor(fields.getField(fieldIdx).typeIdx, strings);
    }
    
    // Get class descriptor
    std::string_view getClassDescriptor(uint32_t classIdx) const {
        if (classIdx >= classes.size()) return {};
        return types.getTypeDescriptor(classes.getClassDef(classIdx).classIdx, strings);
    }
    
    // Get superclass descriptor
    std::string_view getSuperClassDescriptor(uint32_t classIdx) const {
        if (classIdx >= classes.size()) return {};
        return types.getTypeDescriptor(classes.getClassDef(classIdx).superclassIdx, strings);
    }

    // Find method index by class and method name
    int findMethod(uint32_t classIdx, const std::string_view& methodName) const {
        for (uint32_t i = 0; i < methods.size(); ++i) {
            if (methods.getMethod(i).classIdx == classIdx && 
                getMethodName(i) == methodName) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // Find class index by descriptor (returns TYPE index)
    int findClass(const std::string_view& descriptor) const {
        for (uint32_t i = 0; i < types.size(); ++i) {
            if (types.getTypeDescriptor(i, strings) == descriptor) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // Get descriptor directly from a TYPE index (not class def index).
    std::string_view getTypeDescriptor(uint32_t typeIdx) const {
        return types.getTypeDescriptor(typeIdx, strings);
    }

    // Find the class def index whose classIdx field matches the given type index.
    // Returns -1 if no class def references that type.
    int findClassDefByTypeIdx(uint32_t typeIdx) const {
        for (uint32_t i = 0; i < classes.size(); ++i) {
            if (classes.getClassDef(i).classIdx == typeIdx) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // Get superclass descriptor from a TYPE index (resolves through class defs).
    std::string_view getSuperClassByTypeIdx(uint32_t typeIdx) const {
        int defIdx = findClassDefByTypeIdx(typeIdx);
        if (defIdx < 0) return {};
        return types.getTypeDescriptor(classes.getClassDef(defIdx).superclassIdx, strings);
    }

// Helper to pack method key
    static uint64_t makeMethodKey(uint32_t classIdx, uint32_t methodIdx) {
        return (static_cast<uint64_t>(classIdx) << 32) | methodIdx;
    }

    // Build a full prototype descriptor like "(ILjava/lang/String;)V" from
    // a proto index, by reading parameters from the parametersOff offset.
    // The signature layout is read directly from the data section.
    std::string buildProtoString(uint32_t protoIdx) const {
        if (protoIdx >= protos.size()) return {};
        const ProtoIdItem& proto = protos.getProto(protoIdx);
        std::string out = "(";
        if (proto.parametersOff != 0 && proto.parametersOff + 4 <= rawData.size()) {
            uint32_t paramCount = 0;
            std::memcpy(&paramCount, rawData.data() + proto.parametersOff, 4);
            if (paramCount > 16) paramCount = 16; // sanity
            size_t off = proto.parametersOff + 4;
            for (uint32_t i = 0; i < paramCount; ++i) {
                if (off + 2 > rawData.size()) break;
                uint16_t typeIdx = 0;
                std::memcpy(&typeIdx, rawData.data() + off, 2);
                off += 2;
                if (typeIdx < types.size()) {
                    out += std::string(types.getTypeDescriptor(typeIdx, strings));
                }
            }
        }
        out += ")";
        if (proto.returnTypeIdx < types.size()) {
            out += std::string(types.getTypeDescriptor(proto.returnTypeIdx, strings));
        } else {
            out += "V";
        }
        return out;
    }

    // Same as buildProtoString but accepts a DexFile* so it can be called
    // as a static helper.
    static std::string buildProtoString(const DexFile* dex, uint32_t protoIdx) {
        return dex ? dex->buildProtoString(protoIdx) : std::string{};
    }

};

} // namespace NativeDex::Runtime::DexLoader