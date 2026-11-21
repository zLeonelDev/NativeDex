#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <stdexcept>
#include "../dex_loader/DexFile.h"
#include "../object_model/Object.h"
#include "../object_model/ClassObject.h"
#include "../object_model/StringObject.h"
#include "../object_model/ArrayObject.h"
#include "../heap/HeapManager.h"
#include "BytecodeInterpreter.h"

namespace NativeDex::Runtime::BytecodeInterpreter {

using namespace ObjectModel;

class Frame;

class Interpreter {
public:
    Interpreter(BytecodeInterpreter& interpreter);
    ~Interpreter() = default;

    // Execute a method frame
    void execute(Frame* frame);
    
    // Execute until return or exception
    void run(Frame* frame);

private:
    BytecodeInterpreter& interpreter_;
    
    // Core execution step
    void executeInstruction(Frame* frame);
    
    // Opcode implementations
    void executeMove(Frame* frame, uint16_t inst);
    void executeMoveWide(Frame* frame, uint16_t inst);
    void executeMoveObject(Frame* frame, uint16_t inst);
    void executeMoveResult(Frame* frame, uint16_t inst);
    void executeMoveException(Frame* frame, uint16_t inst);
    void executeReturn(Frame* frame, uint16_t inst);
    void executeConst(Frame* frame, uint16_t inst);
    void executeConstWide(Frame* frame, uint16_t inst);
    void executeConstString(Frame* frame, uint16_t inst);
    void executeConstClass(Frame* frame, uint16_t inst);
    void executeMonitor(Frame* frame, uint16_t inst);
    void executeCheckCast(Frame* frame, uint16_t inst);
    void executeInstanceOf(Frame* frame, uint16_t inst);
    void executeArrayLength(Frame* frame, uint16_t inst);
    void executeNewInstance(Frame* frame, uint16_t inst);
    void executeNewArray(Frame* frame, uint16_t inst);
    void executeFilledNewArray(Frame* frame, uint16_t inst);
    void executeFillArrayData(Frame* frame, uint16_t inst);
    void executeThrow(Frame* frame, uint16_t inst);
    void executeGoto(Frame* frame, uint16_t inst);
    void executeSwitch(Frame* frame, uint16_t inst);
    void executeCmp(Frame* frame, uint16_t inst);
    void executeIf(Frame* frame, uint16_t inst);
    void executeIfz(Frame* frame, uint16_t inst);
    void executeArrayGet(Frame* frame, uint16_t inst);
    void executeArrayPut(Frame* frame, uint16_t inst);
    void executeInstanceGet(Frame* frame, uint16_t inst);
    void executeInstancePut(Frame* frame, uint16_t inst);
    void executeStaticGet(Frame* frame, uint16_t inst);
    void executeStaticPut(Frame* frame, uint16_t inst);
    void executeInvoke(Frame* frame, uint16_t inst);
    void executeUnaryOp(Frame* frame, uint16_t inst);
    void executeBinaryOp(Frame* frame, uint16_t inst);
    void executeShiftOp(Frame* frame, uint16_t inst);
    void executeBinaryOpLit(Frame* frame, uint16_t inst);
    void executeConversion(Frame* frame, uint16_t inst);

    // Helper methods
    void invokeMethod(Frame* frame, uint32_t methodIdx, bool isRange, bool isVirtual);
    void invokeNative(Frame* frame, uint32_t methodIdx);
    bool handleException(Frame* frame, Object* exception);
    Frame* findCatchHandler(Frame* frame, ClassObject* exceptionClass);
    Object* resolveClass(Frame* frame, uint32_t classIdx);
    Object* resolveString(Frame* frame, uint32_t stringIdx);
    Object* resolveField(Frame* frame, uint32_t fieldIdx, bool isStatic);
    Object* resolveMethod(Frame* frame, uint32_t methodIdx, bool isStatic);
    
    // Type conversion helpers
    int32_t toInt(uint32_t val) { return static_cast<int32_t>(val); }
    uint32_t toUInt(int32_t val) { return static_cast<uint32_t>(val); }
    int64_t toLong(uint64_t val) { return static_cast<int64_t>(val); }
    uint64_t toULong(int64_t val) { return static_cast<uint64_t>(val); }
    float toFloat(uint32_t val) { return *reinterpret_cast<const float*>(&val); }
    uint32_t toUInt(float val) { return *reinterpret_cast<const uint32_t*>(&val); }
    double toDouble(uint64_t val) { return *reinterpret_cast<const double*>(&val); }
    uint64_t toULong(double val) { return *reinterpret_cast<const uint64_t*>(&val); }
};

} // namespace NativeDex::Runtime::BytecodeInterpreter