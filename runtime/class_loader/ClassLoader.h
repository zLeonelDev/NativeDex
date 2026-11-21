#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include "../dex_loader/DexFile.h"
#include "../object_model/ClassObject.h"

namespace NativeDex::Runtime::ClassLoader {

using ObjectModel::ClassObject;

class ClassLoader {
public:
    explicit ClassLoader(const DexLoader::DexFile* dexFile = nullptr);
    ~ClassLoader();

    // Set the DEX file to load classes from
    void setDexFile(const DexLoader::DexFile* dexFile);

    // Load a class by its descriptor (e.g., "Ljava/lang/String;")
    ClassObject* loadClass(const std::string& descriptor);
    
    // Load a class by its type index
    ClassObject* loadClassByIndex(uint32_t typeIdx);

    // Find an already loaded class
    ClassObject* findClass(const std::string& descriptor) const;
    
    // Get all loaded classes
    const std::unordered_map<std::string, std::unique_ptr<ClassObject>>& getLoadedClasses() const;

    // Initialize a class (run <clinit>)
    bool initializeClass(ClassObject* clazz);

    // Get boot class loader (for system classes)
    static ClassLoader* getBootClassLoader();

private:
    const DexLoader::DexFile* dexFile_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<ClassObject>> loadedClasses_;
    ClassLoader* parent_ = nullptr; // For delegation model

    ClassObject* defineClass(const std::string& descriptor);
    ClassObject* resolveSuperClass(ClassObject* clazz);
    void resolveInterfaces(ClassObject* clazz);
    void loadFields(ClassObject* clazz);
    void loadMethods(ClassObject* clazz);
};

} // namespace NativeDex::Runtime::ClassLoader