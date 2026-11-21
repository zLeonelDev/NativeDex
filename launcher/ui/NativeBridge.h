#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <functional>
#include "../../runtime/bytecode_interpreter/Frame.h"
#include "../../runtime/dex_loader/DexFile.h"
#include <vector>

namespace NativeDex::Runtime::BytecodeInterpreter {
// Frame is now fully defined via Frame.h above.
}

namespace NativeDex::Launcher {

// NativeBridge wires Android framework native methods (the ones the
// interpreter hits when running DEX bytecode) to C++ implementations in
// NativeViewRegistry. Native methods are matched by (class descriptor,
// method name, method prototype), since method indexes differ across the
// multiple DEX files of an APK.
class NativeBridge {
public:
    static NativeBridge& instance();

    // Register a native method for (classDescriptor, methodName, methodProto).
    // `proto` is the full DEX prototype string, e.g. "(Landroid/view/View;)V".
    using NativeHandler = std::function<void(NativeDex::Runtime::BytecodeInterpreter::Frame*)>;
    void registerMethod(const std::string& classDescriptor,
                        const std::string& methodName,
                        const std::string& methodProto,
                        NativeHandler handler);

    // Look up a native handler by (class, method, proto) string keys.
    // Returns nullptr if not registered.
    NativeHandler find(const std::string& classDescriptor,
                       const std::string& methodName,
                       const std::string& methodProto) const;
    bool lookupByMethodIndex(const void* dexFile, uint32_t methodIdx,
                             NativeHandler& outHandler) const;
    // Multi-DEX version: searches all DEX files for the methodIdx.
    bool lookupByMethodIndex(
        const std::vector<const NativeDex::Runtime::DexLoader::DexFile*>& dexFiles,
        uint32_t methodIdx, NativeHandler& outHandler) const;

    // Install all the native method handlers this launcher provides. Idempotent.
    void installDefaults();

private:
    NativeBridge() = default;
    NativeBridge(const NativeBridge&) = delete;
    NativeBridge& operator=(const NativeBridge&) = delete;

    struct Key {
        std::string cls;
        std::string name;
        std::string proto;
        bool operator==(const Key& o) const {
            return cls == o.cls && name == o.name && proto == o.proto;
        }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const noexcept;
    };
    std::unordered_map<Key, NativeHandler, KeyHash> table_;
    bool installed_ = false;
};

} // namespace NativeDex::Launcher
