#include "ClassLoader.h"
#include "../dex_loader/DexFile.h"
#include "../object_model/ClassObject.h"
#include "../object_model/Object.h"
#include "../heap/HeapManager.h"
#include <iostream>

namespace NativeDex::Runtime::ClassLoader {

ClassLoader::ClassLoader(const DexLoader::DexFile* dexFile) : dexFile_(dexFile) {
    if (!dexFile_) {
        // Boot class loader
        parent_ = nullptr;
    }
}

ClassLoader::~ClassLoader() = default;

void ClassLoader::setDexFile(const DexLoader::DexFile* dexFile) {
    dexFile_ = dexFile;
}

ClassObject* ClassLoader::loadClass(const std::string& descriptor) {
    // Check if already loaded
    auto it = loadedClasses_.find(descriptor);
    if (it != loadedClasses_.end()) {
        return it->second.get();
    }

    // Delegate to parent first (boot class loader)
    if (parent_) {
        ClassObject* parentClass = parent_->findClass(descriptor);
        if (parentClass) {
            return parentClass;
        }
    }

    // Not found, define it from our DEX
    return defineClass(descriptor);
}

ClassObject* ClassLoader::loadClassByIndex(uint32_t typeIdx) {
    if (!dexFile_) return nullptr;
    if (typeIdx >= dexFile_->types.size()) return nullptr;
    
    std::string_view descriptor = dexFile_->types.getTypeDescriptor(typeIdx, dexFile_->strings);
    return loadClass(std::string(descriptor));
}

ClassObject* ClassLoader::findClass(const std::string& descriptor) const {
    auto it = loadedClasses_.find(descriptor);
    if (it != loadedClasses_.end()) {
        return it->second.get();
    }
    if (parent_) {
        return parent_->findClass(descriptor);
    }
    return nullptr;
}

const std::unordered_map<std::string, std::unique_ptr<ClassObject>>& ClassLoader::getLoadedClasses() const {
    return loadedClasses_;
}

bool ClassLoader::initializeClass(ClassObject* clazz) {
    if (!clazz || clazz->isInitialized()) return true;
    if (clazz->isInitializing()) {
        // Circular initialization - should throw exception in real impl
        return false;
    }
    
    clazz->setInitializing(true);
    
    // Initialize superclass first
    if (clazz->getSuperClass()) {
        if (!initializeClass(clazz->getSuperClass())) {
            clazz->setInitializing(false);
            return false;
        }
    }
    
    // Run <clinit> if present
    // TODO: Actually invoke the class initializer
    
    clazz->setInitializing(false);
    clazz->setInitialized(true);
    return true;
}

ClassObject* ClassLoader::defineClass(const std::string& descriptor) {
    if (!dexFile_) return nullptr;
    
    // Find class def index by matching type descriptor
    int classDefIdx = -1;
    for (uint32_t i = 0; i < dexFile_->classes.size(); ++i) {
        const auto& classDef = dexFile_->classes.getClassDef(i);
        std::string_view typeDesc = dexFile_->types.getTypeDescriptor(classDef.classIdx, dexFile_->strings);
        if (typeDesc == descriptor) {
            classDefIdx = static_cast<int>(i);
            break;
        }
    }
    
    if (classDefIdx < 0) return nullptr;
    
    const auto& classDef = dexFile_->classes.getClassDef(static_cast<uint32_t>(classDefIdx));
    
    // Create ClassObject
    auto clazz = std::make_unique<ClassObject>(descriptor);
    ClassObject* rawPtr = clazz.get();
    rawPtr->setAccessFlags(classDef.accessFlags);
    rawPtr->setDexClassIndex(classDef.classIdx);
    rawPtr->setClassLoader(this);
    
    // Store before resolving super (to handle circular refs)
    loadedClasses_[descriptor] = std::move(clazz);
    
    // Resolve superclass
    if (classDef.superclassIdx != 0xFFFFFFFF) {
        ClassObject* super = loadClassByIndex(classDef.superclassIdx);
        rawPtr->setSuperClass(super);
    }
    
    // TODO: Resolve interfaces, load fields, load methods
    
    return rawPtr;
}

ClassLoader* ClassLoader::getBootClassLoader() {
    static ClassLoader bootLoader(nullptr);
    return &bootLoader;
}

} // namespace NativeDex::Runtime::ClassLoader