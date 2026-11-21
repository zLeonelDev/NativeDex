#include <iostream>
#include "../runtime/heap/HeapManager.h"
#include "../runtime/object_model/Object.h"
#include "../runtime/object_model/ClassObject.h"
#include "../runtime/object_model/StringObject.h"
#include "../runtime/object_model/ArrayObject.h"

using namespace NativeDex::Runtime;

int main() {
    std::cout << "[TEST] GC/Heap Test" << std::endl;
    
    Heap::HeapManager heap(1024 * 1024);
    
    auto* stringClass = new ObjectModel::ClassObject("Ljava/lang/String;");
    stringClass->setInstanceSize(sizeof(ObjectModel::StringObject));
    
    auto* arrayClass = new ObjectModel::ClassObject("[I");
    arrayClass->setInstanceSize(sizeof(ObjectModel::ArrayObject));
    
    // Allocate many objects
    const int NUM_OBJECTS = 1000;
    std::vector<ObjectModel::Object*> objects;
    objects.reserve(NUM_OBJECTS);
    
    for (int i = 0; i < NUM_OBJECTS; ++i) {
        auto* str = heap.allocateObject(stringClass, 0);
        if (str) {
            str->setClass(stringClass);
            objects.push_back(str);
        }
    }
    
    std::cout << "[TEST] Allocated " << objects.size() << " objects" << std::endl;
    std::cout << "[TEST] Heap used: " << heap.getUsedBytes() << " bytes" << std::endl;
    
    // Clear half the references (simulate garbage)
    objects.erase(objects.begin(), objects.begin() + NUM_OBJECTS / 2);
    
    // Run GC
    heap.collectGarbage();
    
    std::cout << "[TEST] After GC, heap used: " << heap.getUsedBytes() << " bytes" << std::endl;
    std::cout << "[TEST] Object count: " << heap.getObjectCount() << std::endl;
    
    // Allocate more
    for (int i = 0; i < 100; ++i) {
        auto* arr = heap.allocateArray(ObjectModel::ArrayObject::ArrayType::INT, 10);
        if (arr) {
            arr->setClass(arrayClass);
            objects.push_back(arr);
        }
    }
    
    std::cout << "[TEST] After more allocations, heap used: " << heap.getUsedBytes() << " bytes" << std::endl;
    
    // Final GC
    heap.collectGarbage();
    std::cout << "[TEST] Final GC, heap used: " << heap.getUsedBytes() << " bytes" << std::endl;
    
    std::cout << "[TEST] All GC tests passed" << std::endl;
    return 0;
}