#pragma once

#include <iostream>
#include <cassert>
#include "../object_model/Object.h"

namespace NativeDex::Runtime::ObjectModel {

template<typename T>
class Reference {
public:
    // Constructors
    Reference() : ptr_(nullptr) {}
    explicit Reference(T* ptr) : ptr_(ptr) {}
    Reference(const Reference& other) : ptr_(other.ptr_) {}
    Reference(Reference&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    // Destructor
    ~Reference() = default;

    // Assignment operators
    Reference& operator=(const Reference& other) {
        if (this != &other) {
            ptr_ = other.ptr_;
        }
        return *this;
    }

    Reference& operator=(Reference&& other) noexcept {
        if (this != &other) {
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    // Dereference operators
    T& operator*() const {
        assert(ptr_ != nullptr && "Dereferencing null reference");
        return *static_cast<T*>(ptr_);
    }

    T* operator->() const {
        assert(ptr_ != nullptr && "Accessing member of null reference");
        return static_cast<T*>(ptr_);
    }

    // Get the raw pointer (use with caution)
    T* get() const { return static_cast<T*>(ptr_); }

    // Check if null
    bool isNull() const { return ptr_ == nullptr; }

    // Explicit bool operator
    explicit operator bool() const { return ptr_ != nullptr; }

    // Comparison operators
    bool operator==(const Reference& other) const { return ptr_ == other.ptr_; }
    bool operator!=(const Reference& other) const { return ptr_ != other.ptr_; }

    // Reset the reference
    void reset(T* ptr = nullptr) { ptr_ = ptr; }

    // Print the reference (for debugging)
    void print() const {
        if (ptr_) {
            std::cout << "[REF] Pointing to object at " << ptr_ << " (type: " << typeid(T).name() << ")";
        } else {
            std::cout << "[REF] Null reference";
        }
    }

private:
    Object* ptr_; // We store as Object* because all objects in the heap are Objects
};

} // namespace NativeDex::Runtime::ObjectModel