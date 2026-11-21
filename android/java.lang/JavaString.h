#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "../../runtime/object_model/StringObject.h"
#include "../../runtime/object_model/ClassObject.h"

namespace NativeDex::Java::Lang {

using namespace NativeDex::Runtime;

// java.lang.String
class String {
public:
    // Factory methods
    static ObjectModel::StringObject* valueOf(const std::string& str);
    static ObjectModel::StringObject* valueOf(const std::wstring& str);
    static ObjectModel::StringObject* valueOf(int value);
    static ObjectModel::StringObject* valueOf(long value);
    static ObjectModel::StringObject* valueOf(float value);
    static ObjectModel::StringObject* valueOf(double value);
    static ObjectModel::StringObject* valueOf(bool value);
    static ObjectModel::StringObject* valueOf(char value);
    static ObjectModel::StringObject* valueOf(const char* value);
    
    // String operations
    static ObjectModel::StringObject* concat(ObjectModel::StringObject* a, ObjectModel::StringObject* b);
    static bool equals(ObjectModel::StringObject* a, ObjectModel::StringObject* b);
    static int compareTo(ObjectModel::StringObject* a, ObjectModel::StringObject* b);
    static int hashCode(ObjectModel::StringObject* str);
    static int length(ObjectModel::StringObject* str);
    static char charAt(ObjectModel::StringObject* str, int index);
    static ObjectModel::StringObject* substring(ObjectModel::StringObject* str, int beginIndex, int endIndex = -1);
    static int indexOf(ObjectModel::StringObject* str, const std::string& substr, int fromIndex = 0);
    static int lastIndexOf(ObjectModel::StringObject* str, const std::string& substr, int fromIndex = -1);
    static ObjectModel::StringObject* toLowerCase(ObjectModel::StringObject* str);
    static ObjectModel::StringObject* toUpperCase(ObjectModel::StringObject* str);
    static ObjectModel::StringObject* trim(ObjectModel::StringObject* str);
    static std::vector<ObjectModel::StringObject*> split(ObjectModel::StringObject* str, const std::string& regex);
    
    // StringBuilder/StringBuffer support
    class Builder {
    public:
        Builder() = default;
        explicit Builder(int capacity);
        Builder& append(const std::string& str);
        Builder& append(const std::wstring& str);
        Builder& append(int value);
        Builder& append(long value);
        Builder& append(float value);
        Builder& append(double value);
        Builder& append(bool value);
        Builder& append(char value);
        Builder& append(ObjectModel::StringObject* str);
        ObjectModel::StringObject* toString();
    private:
        std::string buffer_;
    };
};

} // namespace NativeDex::Java::Lang