#include "ClassObject.h"
#include <iostream>

namespace NativeDex::Runtime::ObjectModel {

ClassObject::ClassObject(const std::string& descriptor)
    : descriptor_(descriptor), instanceSize_(0) {
    // std::cout << "[CLASS] Class '" << descriptor_ << "' created." << std::endl;
}

std::string ClassObject::getClassName() const {
    // Convert "Ljava/lang/String;" to "java.lang.String"
    if (descriptor_.length() > 2 && descriptor_.front() == 'L' && descriptor_.back() == ';') {
        std::string name = descriptor_.substr(1, descriptor_.length() - 2);
        for (char& c : name) {
            if (c == '/') c = '.';
        }
        return name;
    }
    return descriptor_;
}

std::string ClassObject::getSimpleName() const {
    std::string className = getClassName();
    size_t lastDot = className.find_last_of('.');
    if (lastDot != std::string::npos) {
        return className.substr(lastDot + 1);
    }
    return className;
}

ClassObject::StaticField* ClassObject::findStaticField(const std::string& name, const std::string& descriptor) {
    for (auto& field : staticFields_) {
        if (field.name == name && field.descriptor == descriptor) {
            return &field;
        }
    }
    return nullptr;
}

ClassObject::InstanceField* ClassObject::findInstanceField(const std::string& name, const std::string& descriptor) {
    for (auto& field : instanceFields_) {
        if (field.name == name && field.descriptor == descriptor) {
            return &field;
        }
    }
    if (superClass_) {
        return superClass_->findInstanceField(name, descriptor);
    }
    return nullptr;
}

ClassObject::Method* ClassObject::findMethod(const std::string& name, const std::string& descriptor) {
    for (auto& method : methods_) {
        if (method.name == name && method.descriptor == descriptor) {
            return &method;
        }
    }
    if (superClass_) {
        return superClass_->findMethod(name, descriptor);
    }
    for (auto* iface : interfaces_) {
        if (Method* m = iface->findMethod(name, descriptor)) {
            return m;
        }
    }
    return nullptr;
}

ClassObject::Method* ClassObject::findDirectMethod(const std::string& name, const std::string& descriptor) {
    for (auto& method : methods_) {
        if (method.name == name && method.descriptor == descriptor && 
            (method.accessFlags & (0x0008 | 0x0002 | 0x10000))) { // static, private, or constructor
            return &method;
        }
    }
    return nullptr;
}

ClassObject::Method* ClassObject::findVirtualMethod(const std::string& name, const std::string& descriptor) {
    for (auto& method : methods_) {
        if (method.name == name && method.descriptor == descriptor && 
            !(method.accessFlags & (0x0008 | 0x0002 | 0x10000))) {
            return &method;
        }
    }
    if (superClass_) {
        return superClass_->findVirtualMethod(name, descriptor);
    }
    return nullptr;
}

ClassObject::Method* ClassObject::findStaticMethod(const std::string& name, const std::string& descriptor) {
    for (auto& method : methods_) {
        if (method.name == name && method.descriptor == descriptor && (method.accessFlags & 0x0008)) {
            return &method;
        }
    }
    if (superClass_) {
        return superClass_->findStaticMethod(name, descriptor);
    }
    return nullptr;
}

bool ClassObject::isAssignableFrom(ClassObject* other) const {
    if (other == nullptr) {
        return false;
    }
    if (this == other) {
        return true;
    }
    if (isInterface()) {
        // Check if other implements this interface
        for (auto* iface : other->interfaces_) {
            if (iface == this || isAssignableFrom(iface)) {
                return true;
            }
        }
        if (other->superClass_) {
            return isAssignableFrom(other->superClass_);
        }
        return false;
    } else {
        // Check if other is a subclass
        ClassObject* current = other->superClass_;
        while (current != nullptr) {
            if (current == this) {
                return true;
            }
            current = current->superClass_;
        }
    }
    return false;
}

} // namespace NativeDex::Runtime::ObjectModel