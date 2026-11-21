#include "ArrayObject.h"
#include <cstring>
#include <stdexcept>

namespace NativeDex::Runtime::ObjectModel {

ArrayObject::ArrayObject(ArrayType type, size_t length)
    : elementType_(type), length_(length) {
    switch (type) {
        case ArrayType::BYTE:
        case ArrayType::BOOLEAN:
            elementSize_ = 1; break;
        case ArrayType::CHAR:
        case ArrayType::SHORT:
            elementSize_ = 2; break;
        case ArrayType::INT:
        case ArrayType::FLOAT:
            elementSize_ = 4; break;
        case ArrayType::LONG:
        case ArrayType::DOUBLE:
            elementSize_ = 8; break;
        case ArrayType::OBJECT:
            elementSize_ = sizeof(void*); break;
    }
    dataSize_ = length * elementSize_;
    data_ = reinterpret_cast<uint8_t*>(this) + sizeof(ArrayObject);
}

ArrayObject::ArrayObject(ClassObject* componentType, size_t length)
    : elementType_(ArrayType::OBJECT), componentClass_(componentType), length_(length) {
    elementSize_ = sizeof(void*);
    dataSize_ = length * elementSize_;
    data_ = reinterpret_cast<uint8_t*>(this) + sizeof(ArrayObject);
}

ArrayObject::~ArrayObject() {
}

ArrayObject* ArrayObject::create(ArrayType type, size_t length) {
    // Array creation is handled by HeapManager
    return nullptr;
}

ArrayObject* ArrayObject::createObjectArray(ClassObject* componentType, size_t length) {
    // Array creation is handled by HeapManager
    return nullptr;
}

ArrayObject* ArrayObject::clone() const {
    return nullptr;
}

} // namespace NativeDex::Runtime::ObjectModel
