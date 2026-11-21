#pragma once

#include "Object.h"
#include "ClassObject.h"
#include <string>
#include <string_view>

namespace NativeDex::Runtime::ObjectModel {

class StringObject : public Object {
public:
    // Create from UTF-8 string
    static StringObject* create(const std::string& utf8);
    static StringObject* createFromMutf8(const std::string_view& mutf8);
    
    StringObject() = default;
    ~StringObject() override = default;

    // Get as UTF-8
    std::string toUtf8() const { return value_; }
    std::string_view toUtf8View() const { return value_; }
    
    // Replace the contents (used by String.<init> overloads).
    void setUtf8(const std::string& utf8) {
        value_ = utf8;
        hashComputed_ = false;
        u16Valid_ = false;
    }

    // Get as UTF-16 (Java char[])
    std::u16string toUtf16() const;
    // Cached UTF-16 view (rebuilt lazily after setUtf8).
    const std::u16string& u16() const;
    size_t utf16Length() const;
    
    // Get char at index (UTF-16 code unit)
    char16_t charAt(size_t index) const;
    
    // Length in Java chars (UTF-16 code units)
    size_t length() const { return utf16Length(); }
    
    // Hash code (java.lang.String.hashCode())
    int32_t hashCode() const;

    // Compare
    int compareTo(const StringObject* other) const;
    bool equals(const StringObject* other) const;

    // Object interface
    size_t objectSize() const override { return sizeof(StringObject) + value_.capacity(); }
    void visitReferences(void (*visitor)(Object**)) override {}

private:
    std::string value_; // Stored as UTF-8 internally
    mutable int32_t cachedHash_ = 0;
    mutable bool hashComputed_ = false;
    mutable std::u16string u16Cache_;
    mutable bool u16Valid_ = false;
};

} // namespace NativeDex::Runtime::ObjectModel