#include "JavaString.h"
#include "../../runtime/object_model/StringObject.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace NativeDex::Java::Lang {

ObjectModel::StringObject* String::valueOf(const std::string& str) {
    return ObjectModel::StringObject::create(str);
}

ObjectModel::StringObject* String::valueOf(const std::wstring& str) {
    std::string utf8;
    utf8.reserve(str.size());
    for (wchar_t c : str) {
        if (c <= 0x7F) {
            utf8.push_back(static_cast<char>(c));
        } else if (c <= 0x7FF) {
            utf8.push_back(static_cast<char>(0xC0 | (c >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xE0 | (c >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return ObjectModel::StringObject::create(utf8);
}

ObjectModel::StringObject* String::valueOf(int value) {
    return ObjectModel::StringObject::create(std::to_string(value));
}

ObjectModel::StringObject* String::valueOf(long value) {
    return ObjectModel::StringObject::create(std::to_string(value));
}

ObjectModel::StringObject* String::valueOf(float value) {
    return ObjectModel::StringObject::create(std::to_string(value));
}

ObjectModel::StringObject* String::valueOf(double value) {
    return ObjectModel::StringObject::create(std::to_string(value));
}

ObjectModel::StringObject* String::valueOf(bool value) {
    return ObjectModel::StringObject::create(value ? "true" : "false");
}

ObjectModel::StringObject* String::valueOf(char value) {
    return ObjectModel::StringObject::create(std::string(1, value));
}

ObjectModel::StringObject* String::valueOf(const char* value) {
    if (!value) return ObjectModel::StringObject::create("null");
    return ObjectModel::StringObject::create(value);
}

ObjectModel::StringObject* String::concat(ObjectModel::StringObject* a, ObjectModel::StringObject* b) {
    if (!a) return b;
    if (!b) return a;
    std::string result = a->toUtf8() + b->toUtf8();
    return ObjectModel::StringObject::create(result);
}

bool String::equals(ObjectModel::StringObject* a, ObjectModel::StringObject* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return a->equals(b);
}

int String::compareTo(ObjectModel::StringObject* a, ObjectModel::StringObject* b) {
    if (!a) return b ? -1 : 0;
    if (!b) return 1;
    return a->compareTo(b);
}

int String::hashCode(ObjectModel::StringObject* str) {
    return str ? str->hashCode() : 0;
}

int String::length(ObjectModel::StringObject* str) {
    return str ? static_cast<int>(str->length()) : 0;
}

char String::charAt(ObjectModel::StringObject* str, int index) {
    return str ? static_cast<char>(str->charAt(index)) : 0;
}

ObjectModel::StringObject* String::substring(ObjectModel::StringObject* str, int beginIndex, int endIndex) {
    if (!str) return nullptr;
    std::u16string u16 = str->toUtf16();
    if (endIndex < 0) endIndex = static_cast<int>(u16.size());
    if (beginIndex < 0) beginIndex = 0;
    if (beginIndex >= static_cast<int>(u16.size()) || beginIndex > endIndex) {
        return ObjectModel::StringObject::create("");
    }
    std::u16string sub = u16.substr(beginIndex, endIndex - beginIndex);
    // Convert back to UTF-8
    std::string utf8;
    for (char16_t c : sub) {
        if (c <= 0x7F) {
            utf8.push_back(static_cast<char>(c));
        } else if (c <= 0x7FF) {
            utf8.push_back(static_cast<char>(0xC0 | (c >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xE0 | (c >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return ObjectModel::StringObject::create(utf8);
}

int String::indexOf(ObjectModel::StringObject* str, const std::string& substr, int fromIndex) {
    if (!str) return -1;
    std::string haystack = str->toUtf8();
    if (fromIndex < 0) fromIndex = 0;
    if (fromIndex >= static_cast<int>(haystack.size())) return -1;
    size_t pos = haystack.find(substr, fromIndex);
    return pos != std::string::npos ? static_cast<int>(pos) : -1;
}

int String::lastIndexOf(ObjectModel::StringObject* str, const std::string& substr, int fromIndex) {
    if (!str) return -1;
    std::string haystack = str->toUtf8();
    if (fromIndex < 0) fromIndex = static_cast<int>(haystack.size()) - 1;
    if (fromIndex >= static_cast<int>(haystack.size())) fromIndex = static_cast<int>(haystack.size()) - 1;
    size_t pos = haystack.rfind(substr, fromIndex);
    return pos != std::string::npos ? static_cast<int>(pos) : -1;
}

ObjectModel::StringObject* String::toLowerCase(ObjectModel::StringObject* str) {
    if (!str) return nullptr;
    std::string s = str->toUtf8();
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return ObjectModel::StringObject::create(s);
}

ObjectModel::StringObject* String::toUpperCase(ObjectModel::StringObject* str) {
    if (!str) return nullptr;
    std::string s = str->toUtf8();
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return ObjectModel::StringObject::create(s);
}

ObjectModel::StringObject* String::trim(ObjectModel::StringObject* str) {
    if (!str) return nullptr;
    std::string s = str->toUtf8();
    size_t start = s.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) return ObjectModel::StringObject::create("");
    size_t end = s.find_last_not_of(" \t\n\r\f\v");
    return ObjectModel::StringObject::create(s.substr(start, end - start + 1));
}

std::vector<ObjectModel::StringObject*> String::split(ObjectModel::StringObject* str, const std::string& regex) {
    std::vector<ObjectModel::StringObject*> result;
    if (!str) return result;
    // Simplified - would need regex library for full implementation
    std::string s = str->toUtf8();
    size_t start = 0;
    size_t pos = s.find(regex);
    while (pos != std::string::npos) {
        result.push_back(ObjectModel::StringObject::create(s.substr(start, pos - start)));
        start = pos + regex.length();
        pos = s.find(regex, start);
    }
    result.push_back(ObjectModel::StringObject::create(s.substr(start)));
    return result;
}

String::Builder::Builder(int capacity) {
    buffer_.reserve(capacity);
}

String::Builder& String::Builder::append(const std::string& str) {
    buffer_ += str;
    return *this;
}

String::Builder& String::Builder::append(const std::wstring& str) {
    for (wchar_t c : str) {
        if (c <= 0x7F) buffer_.push_back(static_cast<char>(c));
        else if (c <= 0x7FF) {
            buffer_.push_back(static_cast<char>(0xC0 | (c >> 6)));
            buffer_.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            buffer_.push_back(static_cast<char>(0xE0 | (c >> 12)));
            buffer_.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            buffer_.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return *this;
}

String::Builder& String::Builder::append(int value) {
    buffer_ += std::to_string(value);
    return *this;
}

String::Builder& String::Builder::append(long value) {
    buffer_ += std::to_string(value);
    return *this;
}

String::Builder& String::Builder::append(float value) {
    buffer_ += std::to_string(value);
    return *this;
}

String::Builder& String::Builder::append(double value) {
    buffer_ += std::to_string(value);
    return *this;
}

String::Builder& String::Builder::append(bool value) {
    buffer_ += value ? "true" : "false";
    return *this;
}

String::Builder& String::Builder::append(char value) {
    buffer_.push_back(value);
    return *this;
}

String::Builder& String::Builder::append(ObjectModel::StringObject* str) {
    if (str) buffer_ += str->toUtf8();
    else buffer_ += "null";
    return *this;
}

ObjectModel::StringObject* String::Builder::toString() {
    return ObjectModel::StringObject::create(buffer_);
}

} // namespace NativeDex::Java::Lang