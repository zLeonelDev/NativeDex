#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace NativeDex::Runtime::ObjectModel {

class Object;  // Forward declaration

class FieldStorage {
public:
    FieldStorage() = default;
    ~FieldStorage() = default;

    // Set a field value by name (object reference)
    void setObjectField(const std::string& name, Object* value);

    // Get a field value by name (object reference)
    Object* getObjectField(const std::string& name) const;

    // Set a field value by name (integer value)
    void setIntField(const std::string& name, int32_t value);

    // Get a field value by name (integer value)
    int32_t getIntField(const std::string& name) const;

    // Set a field value by name (boolean value)
    void setBooleanField(const std::string& name, bool value);

    // Get a field value by name (boolean value)
    bool getBooleanField(const std::string& name) const;

    // Check if a field exists
    bool hasField(const std::string& name) const;

    // Remove a field
    void removeField(const std::string& name);

    // Get all field names
    std::vector<std::string> getFieldNames() const;

private:
    // We'll use a simple map for now, but in a real VM we'd use indexed access
    struct FieldValue {
        enum Type { OBJECT, INTEGER, BOOLEAN } type;
        union {
            Object* object;
            int32_t integer;
            bool boolean;
        } value;
    };

    std::unordered_map<std::string, FieldValue> fields_;
};

} // namespace NativeDex::Runtime::ObjectModel