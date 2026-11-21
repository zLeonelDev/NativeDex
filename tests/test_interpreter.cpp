#include <iostream>
#include <memory>
#include "../runtime/bytecode_interpreter/BytecodeInterpreter.h"
#include "../runtime/heap/HeapManager.h"
#include "../runtime/object_model/Object.h"
#include "../runtime/object_model/ClassObject.h"
#include "../runtime/object_model/StringObject.h"
#include "../runtime/object_model/ArrayObject.h"

using namespace NativeDex::Runtime;

int main() {
    std::cout << "[TEST] Interpreter Test" << std::endl;
    
    // Test heap allocation
    Heap::HeapManager heap(1024 * 1024);
    
    // Test ClassObject
    auto* stringClass = new ObjectModel::ClassObject("Ljava/lang/String;");
    stringClass->setAccessFlags(0x0001); // public
    stringClass->setInstanceSize(sizeof(ObjectModel::StringObject));
    
    std::cout << "[TEST] ClassObject created: " << stringClass->getDescriptor() << std::endl;
    
    // Test StringObject
    auto* str = ObjectModel::StringObject::create("Hello, NativeDex!");
    str->setClass(stringClass);
    
    std::cout << "[TEST] StringObject: " << str->toUtf8() << std::endl;
    std::cout << "[TEST] String hash: " << str->hashCode() << std::endl;
    std::cout << "[TEST] String length: " << str->length() << std::endl;
    
    // Test ArrayObject
    auto* intArray = ObjectModel::ArrayObject::create(ObjectModel::ArrayObject::ArrayType::INT, 10);
    intArray->setClass(stringClass); // Just for testing
    
    for (int i = 0; i < 10; ++i) {
        intArray->set<int>(i, i * 2);
    }
    
    std::cout << "[TEST] ArrayObject length: " << intArray->length() << std::endl;
    std::cout << "[TEST] Array[5] = " << intArray->get<int>(5) << std::endl;
    
    // Test heap allocation
    auto* obj = heap.allocateObject(stringClass, 0);
    if (obj) {
        obj->setClass(stringClass);
        std::cout << "[TEST] Heap allocation successful" << std::endl;
        std::cout << "[TEST] Heap used: " << heap.getUsedBytes() << " bytes" << std::endl;
    }
    
    // Test GC
    heap.collectGarbage();
    std::cout << "[TEST] GC completed" << std::endl;
    
    std::cout << "[TEST] All interpreter tests passed" << std::endl;
    return 0;
}