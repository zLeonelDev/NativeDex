#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "../../runtime/dex_loader/DexFile.h"

namespace NativeDex::Launcher {

struct BootstrapResult {
    bool success = false;
    std::string errorMessage;
    std::string packageName;
    std::string launcherActivity;
    std::string applicationClass;
    std::string appLabel;
    std::vector<std::vector<uint8_t>> dexDatas;
    std::vector<NativeDex::Runtime::DexLoader::DexFile> dexFiles;
};

class Bootstrap {
public:
    struct Options {
        bool debug = false;
        bool scanOnly = false;
        std::string apkPath;
    };
    
    explicit Bootstrap(Options options);
    ~Bootstrap() = default;
    
    void startAsync(std::function<void(BootstrapResult)> callback);

private:
    Options options_;
    
    bool parseApk();
    bool parseManifest(BootstrapResult& result, const std::vector<uint8_t>& manifestData);
    bool extractDexFiles(BootstrapResult& result);
};

} // namespace NativeDex::Launcher