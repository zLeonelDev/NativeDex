#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <functional>
#include "Object.h"
#include "../dex_loader/DexFile.h"

namespace NativeDex::Runtime::ClassLoader {
class ClassLoader;
}

namespace NativeDex::Runtime::ObjectModel {

class ClassObject : public Object {
public:
    ClassObject(const std::string& descriptor);
    virtual ~ClassObject() = default;

    // Class descriptor (e.g., "Ljava/lang/String;")
    const std::string& getDescriptor() const { return descriptor_; }
    
    // Java class name (e.g., "java.lang.String")
    std::string getClassName() const;
    
    // Simple name (e.g., "String")
    std::string getSimpleName() const;

    // Superclass
    ClassObject* getSuperClass() const { return superClass_; }
    void setSuperClass(ClassObject* super) { superClass_ = super; }

    // Interfaces
    const std::vector<ClassObject*>& getInterfaces() const { return interfaces_; }
    void addInterface(ClassObject* iface) { interfaces_.push_back(iface); }

    // Class loader
    NativeDex::Runtime::ClassLoader::ClassLoader* getClassLoader() const { return classLoader_; }
    void setClassLoader(NativeDex::Runtime::ClassLoader::ClassLoader* loader) { classLoader_ = loader; }

    // DEX class index
    uint32_t getDexClassIndex() const { return dexClassIndex_; }
    void setDexClassIndex(uint32_t idx) { dexClassIndex_ = idx; }

    // Access flags
    uint32_t getAccessFlags() const { return accessFlags_; }
    void setAccessFlags(uint32_t flags) { accessFlags_ = flags; }
    bool isPublic() const { return (accessFlags_ & 0x0001) != 0; }
    bool isPrivate() const { return (accessFlags_ & 0x0002) != 0; }
    bool isProtected() const { return (accessFlags_ & 0x0004) != 0; }
    bool isStatic() const { return (accessFlags_ & 0x0008) != 0; }
    bool isFinal() const { return (accessFlags_ & 0x0010) != 0; }
    bool isInterface() const { return (accessFlags_ & 0x0200) != 0; }
    bool isAbstract() const { return (accessFlags_ & 0x0400) != 0; }
    bool isSynthetic() const { return (accessFlags_ & 0x1000) != 0; }
    bool isAnnotation() const { return (accessFlags_ & 0x2000) != 0; }
    bool isEnum() const { return (accessFlags_ & 0x4000) != 0; }

    // Instance size (for object allocation)
    uint32_t getInstanceSize() const { return instanceSize_; }
    void setInstanceSize(uint32_t size) { instanceSize_ = size; }

    // Object alignment
    uint32_t getObjectAlignment() const { return objectAlignment_; }
    void setObjectAlignment(uint32_t align) { objectAlignment_ = align; }

    // Static fields
    struct StaticField {
        std::string name;
        std::string descriptor;
        uint32_t accessFlags;
        uint32_t offset; // Offset in static field area
        // Value storage (simplified - in reality would be typed)
        uint64_t value = 0;
    };
    
    void addStaticField(const StaticField& field) { staticFields_.push_back(field); }
    const std::vector<StaticField>& getStaticFields() const { return staticFields_; }
    StaticField* findStaticField(const std::string& name, const std::string& descriptor);
    
    // Instance fields
    struct InstanceField {
        std::string name;
        std::string descriptor;
        uint32_t accessFlags;
        uint32_t offset; // Offset from object header
    };
    
    void addInstanceField(const InstanceField& field) { instanceFields_.push_back(field); }
    const std::vector<InstanceField>& getInstanceFields() const { return instanceFields_; }
    InstanceField* findInstanceField(const std::string& name, const std::string& descriptor);

    // Methods
    struct Method {
        std::string name;
        std::string descriptor;
        std::string shorty; // Short signature
        uint32_t accessFlags;
        uint32_t codeOff = 0; // DEX code offset
        uint16_t registersSize = 0;
        uint16_t insSize = 0;
        uint16_t outsSize = 0;
        // For native methods
        std::function<void(Object*, uint32_t*)> nativeFunc = nullptr;
        // For interpreted methods - reference to DEX CodeItem
        const DexLoader::CodeItem* codeItem = nullptr;
        // For JIT
        void* compiledCode = nullptr;
    };
    
    void addMethod(const Method& method) { methods_.push_back(method); }
    const std::vector<Method>& getMethods() const { return methods_; }
    Method* findMethod(const std::string& name, const std::string& descriptor);
    Method* findDirectMethod(const std::string& name, const std::string& descriptor);
    Method* findVirtualMethod(const std::string& name, const std::string& descriptor);
    Method* findStaticMethod(const std::string& name, const std::string& descriptor);

    // Class initialization
    bool isInitialized() const { return initialized_; }
    void setInitialized(bool v) { initialized_ = v; }
    bool isInitializing() const { return initializing_; }
    void setInitializing(bool v) { initializing_ = v; }

    // Verify if this class is assignable from another
    bool isAssignableFrom(ClassObject* other) const;

    // vtable for virtual dispatch
    std::vector<Method*> vtable_;
    
    // Create new instance
    virtual Object* newInstance() const { return nullptr; }

    // Object interface
    size_t objectSize() const override { return sizeof(ClassObject); }
    void visitReferences(void (*visitor)(Object**)) override {
        visitor(reinterpret_cast<Object**>(&superClass_));
        for (auto* iface : interfaces_) visitor(reinterpret_cast<Object**>(&iface));
        if (classLoader_) visitor(reinterpret_cast<Object**>(&classLoader_));
    }

protected:
    std::string descriptor_;
    ClassObject* superClass_ = nullptr;
    std::vector<ClassObject*> interfaces_;
    NativeDex::Runtime::ClassLoader::ClassLoader* classLoader_ = nullptr;
    uint32_t dexClassIndex_ = 0;
    uint32_t accessFlags_ = 0;
    uint32_t instanceSize_ = 0;
    uint32_t objectAlignment_ = 8;
    std::vector<StaticField> staticFields_;
    std::vector<InstanceField> instanceFields_;
    std::vector<Method> methods_;
    bool initialized_ = false;
    bool initializing_ = false;
};

} // namespace NativeDex::Runtime::ObjectModel