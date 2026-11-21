#include "StringObject.h"
#include <algorithm>
#include <codecvt>
#include <locale>

namespace NativeDex::Runtime::ObjectModel {

StringObject* StringObject::create(const std::string& utf8) {
    StringObject* obj = new StringObject();
    obj->value_ = utf8;
    return obj;
}



StringObject* StringObject::createFromMutf8(const std::string_view& mutf8) {
    std::string utf8;
    utf8.reserve(mutf8.size());
    for (size_t i = 0; i < mutf8.size(); ++i) {
        uint8_t b = static_cast<uint8_t>(mutf8[i]);
        if (b == 0) break;
        if (b == 0xC0 && i + 1 < mutf8.size() && static_cast<uint8_t>(mutf8[i + 1]) == 0x80) {
            utf8.push_back('\0');
            ++i;
        } else {
            utf8.push_back(static_cast<char>(b));
        }
    }
    return create(utf8);
}

std::u16string StringObject::toUtf16() const {
    std::u16string result;
    result.reserve(value_.size());
    for (size_t i = 0; i < value_.size(); ) {
        uint8_t b = static_cast<uint8_t>(value_[i]);
        if ((b & 0x80) == 0) {
            result.push_back(static_cast<char16_t>(b));
            ++i;
        } else if ((b & 0xE0) == 0xC0) {
            if (i + 1 >= value_.size()) break;
            char16_t c = ((b & 0x1F) << 6) | (static_cast<uint8_t>(value_[i + 1]) & 0x3F);
            result.push_back(c);
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            if (i + 2 >= value_.size()) break;
            char16_t c = ((b & 0x0F) << 12) | ((static_cast<uint8_t>(value_[i + 1]) & 0x3F) << 6) |
                         (static_cast<uint8_t>(value_[i + 2]) & 0x3F);
            result.push_back(c);
            i += 3;
        } else if ((b & 0xF8) == 0xF0) {
            if (i + 3 >= value_.size()) break;
            uint32_t cp = ((b & 0x07) << 18) | ((static_cast<uint8_t>(value_[i + 1]) & 0x3F) << 12) |
                          ((static_cast<uint8_t>(value_[i + 2]) & 0x3F) << 6) | (static_cast<uint8_t>(value_[i + 3]) & 0x3F);
            cp -= 0x10000;
            result.push_back(static_cast<char16_t>(0xD800 | (cp >> 10)));
            result.push_back(static_cast<char16_t>(0xDC00 | (cp & 0x3FF)));
            i += 4;
        } else {
            result.push_back(static_cast<char16_t>(b));
            ++i;
        }
    }
    return result;
}

const std::u16string& StringObject::u16() const {
    if (!u16Valid_) {
        u16Cache_ = toUtf16();
        u16Valid_ = true;
    }
    return u16Cache_;
}

size_t StringObject::utf16Length() const {
    return u16().size();
}

char16_t StringObject::charAt(size_t index) const {
    const std::u16string& s = u16();
    if (index >= s.size()) throw std::out_of_range("String index out of bounds");
    return s[index];
}

int32_t StringObject::hashCode() const {
    if (hashComputed_) return cachedHash_;
    uint32_t h = 0;
    for (char16_t c : u16()) {
        h = 31u * h + static_cast<uint32_t>(c);
    }
    cachedHash_ = static_cast<int32_t>(h);
    hashComputed_ = true;
    return cachedHash_;
}

int StringObject::compareTo(const StringObject* other) const {
    if (!other) return 1;
    const std::u16string& a = u16();
    const std::u16string& b = other->u16();
    size_t minLen = std::min(a.size(), b.size());
    for (size_t i = 0; i < minLen; ++i) {
        if (a[i] != b[i]) return static_cast<int>(a[i]) - static_cast<int>(b[i]);
    }
    return static_cast<int>(a.size()) - static_cast<int>(b.size());
}

bool StringObject::equals(const StringObject* other) const {
    if (!other) return false;
    if (this == other) return true;
    return value_ == other->value_;
}

} // namespace NativeDex::Runtime::ObjectModel