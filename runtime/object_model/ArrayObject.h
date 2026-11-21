#pragma once

#include "Object.h"
#include "ClassObject.h"
#include <vector>
#include <cstdint>
#include <memory>

namespace NativeDex::Runtime::ObjectModel {

class ArrayObject : public Object {
public:
    enum class ArrayType : uint8_t {
        BYTE    = 0, // [B
        CHAR    = 1, // [C
        SHORT   = 2, // [S
        INT     = 3, // [I
        LONG    = 4, // [J
        FLOAT   = 5, // [F
        DOUBLE  = 6, // [D
        BOOLEAN = 7, // [Z
        OBJECT  = 8  // [L...; or [[...
    };

    // Create array of given type and length
    static ArrayObject* create(ArrayType type, size_t length);
    static ArrayObject* createObjectArray(ClassObject* componentType, size_t length);
    
    ArrayObject(ArrayType type, size_t length);
    ArrayObject(ClassObject* componentType, size_t length);
    ~ArrayObject() override;

    // Get length
    size_t length() const { return length_; }
    
    // Get element type
    ArrayType getElementType() const { return elementType_; }
    ClassObject* getComponentClass() const { return componentClass_; }
    void setComponentClass(ClassObject* klass) { componentClass_ = klass; }
    
    // Check if object array
    bool isObjectArray() const { return elementType_ == ArrayType::OBJECT; }
    bool isPrimitiveArray() const { return elementType_ != ArrayType::OBJECT; }

    // Get/set elements (with bounds checking)
    template<typename T>
    T get(size_t index) const {
        if (index >= length_) throw std::out_of_range("Array index out of bounds");
        return *reinterpret_cast<const T*>(data_ + index * elementSize_);
    }
    
    template<typename T>
    void set(size_t index, T value) {
        if (index >= length_) throw std::out_of_range("Array index out of bounds");
        *reinterpret_cast<T*>(data_ + index * elementSize_) = value;
    }

    // Raw data access
    void* data() { return data_; }
    const void* data() const { return data_; }

    // For object arrays
    Object* getObject(size_t index) const {
        if (index >= length_) throw std::out_of_range("Array index out of bounds");
        return reinterpret_cast<Object**>(data_)[index];
    }
    
    void setObject(size_t index, Object* value) {
        if (index >= length_) throw std::out_of_range("Array index out of bounds");
        reinterpret_cast<Object**>(data_)[index] = value;
    }

    // Multi-dimensional array support
    ArrayObject* getArray(size_t index) const {
        Object* obj = getObject(index);
        return dynamic_cast<ArrayObject*>(obj);
    }

    // Object interface
    size_t objectSize() const override { return sizeof(ArrayObject) + dataSize_; }
    void visitReferences(void (*visitor)(Object**)) override {
        if (isObjectArray()) {
            for (size_t i = 0; i < length_; ++i) {
                Object* obj = getObject(i);
                if (obj) visitor(&reinterpret_cast<Object**>(data_)[i]);
            }
        }
    }

    // Clone array
    ArrayObject* clone() const;

    // Fill array
    template<typename T>
    void fill(const T& value) {
        for (size_t i = 0; i < length_; ++i) {
            set<T>(i, value);
        }
    }

private:
    ArrayType elementType_ = ArrayType::BYTE;
    ClassObject* componentClass_ = nullptr;
    size_t length_ = 0;
    size_t elementSize_ = 1;
    size_t dataSize_ = 0;
    uint8_t* data_ = nullptr; // Allocated with object
};

} // namespace NativeDex::Runtime::ObjectModel