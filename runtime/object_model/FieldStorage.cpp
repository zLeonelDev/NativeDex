#include "FieldStorage.h"
#include <iostream>
#include <vector>

namespace NativeDex::Runtime::ObjectModel {

void FieldStorage::setObjectField(const std::string& name, Object* value) {
    FieldValue fv;
    fv.type = FieldValue::OBJECT;
    fv.value.object = value;
    fields_[name] = fv;
    std::cout << "[FIELD] Set object field '" << name << "' to " << value << std::endl;
}

Object* FieldStorage::getObjectField(const std::string& name) const {
    auto it = fields_.find(name);
    if (it != fields_.end() && it->second.type == FieldValue::OBJECT) {
        std::cout << "[FIELD] Got object field '" << name << "' = " << it->second.value.object << std::endl;
        return it->second.value.object;
    }
    std::cout << "[FIELD] Got object field '" << name << "' = nullptr (not found or type mismatch)" << std::endl;
    return nullptr;
}

void FieldStorage::setIntField(const std::string& name, int32_t value) {
    FieldValue fv;
    fv.type = FieldValue::INTEGER;
    fv.value.integer = value;
    fields_[name] = fv;
    std::cout << "[FIELD] Set int field '" << name << "' to " << value << std::endl;
}

int32_t FieldStorage::getIntField(const std::string& name) const {
    auto it = fields_.find(name);
    if (it != fields_.end() && it->second.type == FieldValue::INTEGER) {
        std::cout << "[FIELD] Got int field '" << name << "' = " << it->second.value.integer << std::endl;
        return it->second.value.integer;
    }
    std::cout << "[FIELD] Got int field '" << name << "' = 0 (not found or type mismatch)" << std::endl;
    return 0;
}

void FieldStorage::setBooleanField(const std::string& name, bool value) {
    FieldValue fv;
    fv.type = FieldValue::BOOLEAN;
    fv.value.boolean = value;
    fields_[name] = fv;
    std::cout << "[FIELD] Set boolean field '" << name << "' to " << (value ? "true" : "false") << std::endl;
}

bool FieldStorage::getBooleanField(const std::string& name) const {
    auto it = fields_.find(name);
    if (it != fields_.end() && it->second.type == FieldValue::BOOLEAN) {
        std::cout << "[FIELD] Got boolean field '" << name << "' = " << (it->second.value.boolean ? "true" : "false") << std::endl;
        return it->second.value.boolean;
    }
    std::cout << "[FIELD] Got boolean field '" << name << "' = false (not found or type mismatch)" << std::endl;
    return false;
}

bool FieldStorage::hasField(const std::string& name) const {
    return fields_.find(name) != fields_.end();
}

void FieldStorage::removeField(const std::string& name) {
    auto it = fields_.find(name);
    if (it != fields_.end()) {
        fields_.erase(it);
        std::cout << "[FIELD] Removed field '" << name << "'" << std::endl;
    }
}

std::vector<std::string> FieldStorage::getFieldNames() const {
    std::vector<std::string> names;
    for (const auto& pair : fields_) {
        names.push_back(pair.first);
    }
    return names;
}

} // namespace NativeDex::Runtime::ObjectModel