#pragma once

#include "BinaryReader.h"
#include "DexFile.h"
#include "StringTable.h"
#include "TypeTable.h"
#include "ProtoTable.h"
#include "FieldTable.h"
#include "MethodTable.h"
#include "ClassTable.h"

namespace NativeDex::Runtime::DexLoader {

class Parser {
public:
    explicit Parser(BinaryReader& reader) : reader_(reader) {}

    void parse(DexFile& dexFile);

private:
    BinaryReader& reader_;
};

} // namespace NativeDex::Runtime::DexLoader