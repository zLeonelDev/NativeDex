#include "Bootstrap.h"
#include "AndroidManifest.h"
#include "../../runtime/dex_loader/DexLoader.h"
#include "../../platform/windows/filesystem/Win32FileSystem.h"
#include <iostream>
#include <thread>
#include <future>
#include <algorithm>
#include <filesystem>
#include <miniz.h>

namespace NativeDex::Launcher {

Bootstrap::Bootstrap(Options options) : options_(std::move(options)) {}

void Bootstrap::startAsync(std::function<void(BootstrapResult)> callback) {
    std::thread([this, callback = std::move(callback)]() mutable {
        BootstrapResult result;
        
        if (!parseApk()) {
            result.success = false;
            result.errorMessage = "Failed to parse APK";
            callback(std::move(result));
            return;
        }
        
        if (!extractDexFiles(result)) {
            result.success = false;
            result.errorMessage = "Failed to extract DEX files";
            callback(std::move(result));
            return;
        }
        
        // Load DEX files
        for (const auto& dexData : result.dexDatas) {
            NativeDex::Runtime::DexLoader::DexLoader dexLoader;
            if (!dexLoader.loadFromMemory(dexData)) {
                result.success = false;
                result.errorMessage = "Failed to load DEX file";
                callback(std::move(result));
                return;
            }
            result.dexFiles.push_back(std::move(dexLoader.getDexFile()));
        }
        
        // Fallback: If no launcher activity from manifest, find one named "MainActivity"
        if (result.launcherActivity.empty() && !result.dexFiles.empty()) {
            const auto& mainDex = result.dexFiles[0];
            for (uint32_t i = 0; i < mainDex.classes.size(); ++i) {
                std::string desc(mainDex.getClassDescriptor(i));
                if (desc.find("MainActivity") != std::string::npos) {
                    // Convert descriptor Lcom/example/MainActivity; to com.example.MainActivity
                    if (desc.length() > 2 && desc[0] == 'L' && desc.back() == ';') {
                        desc = desc.substr(1, desc.length() - 2);
                        std::replace(desc.begin(), desc.end(), '/', '.');
                        result.launcherActivity = desc;
                        break;
                    }
                }
            }
        }
        
        result.success = true;
        callback(std::move(result));
    }).detach();
}

bool Bootstrap::parseApk() {
    // Validate the APK exists; the actual read + ZIP parsing happens in extractDexFiles
    return std::filesystem::exists(options_.apkPath);
}

bool Bootstrap::parseManifest(BootstrapResult& result, const std::vector<uint8_t>& manifestData) {
    BinaryXmlParser parser(manifestData);
    AndroidManifest manifest;
    if (!parser.parse(manifest)) {
        std::cerr << "[NativeDex] Bootstrap: AndroidManifest.xml is not a valid binary XML document." << std::endl;
        return false;
    }
    result.packageName = manifest.packageName;
    result.appLabel = manifest.appLabel;
    result.launcherActivity = manifest.launcherActivity;
    result.applicationClass = manifest.applicationClass;
    std::cout << "[NativeDex] Bootstrap: Manifest package=\"" << result.packageName
              << "\" label=\"" << result.appLabel
              << "\" launcher=\"" << result.launcherActivity << "\"" << std::endl;
    return true;
}

bool Bootstrap::extractDexFiles(BootstrapResult& result) {
    auto data = NativeDex::Platform::Windows::Win32FileSystem::readAllBytes(options_.apkPath);
    if (data.empty()) {
        std::cerr << "[NativeDex] Bootstrap: APK is empty or unreadable." << std::endl;
        return false;
    }
    
    std::cout << "[NativeDex] Bootstrap: Read APK, " << data.size() << " bytes." << std::endl;
    
    // Check if it's a raw DEX file
    if (data.size() >= 8 && 
        data[0] == 'd' && data[1] == 'e' && data[2] == 'x' && data[3] == '\n') {
        result.dexDatas.push_back(std::move(data));
        std::cout << "[NativeDex] Bootstrap: Input is a raw DEX file." << std::endl;
        return true;
    }
    
    // Attempt to read as ZIP using miniz
    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));
    if (!mz_zip_reader_init_mem(&zip_archive, data.data(), data.size(), 0)) {
        mz_zip_error err = mz_zip_get_last_error(&zip_archive);
        std::cerr << "[NativeDex] Bootstrap: Failed to init ZIP reader. "
                  << "miniz error=" << static_cast<int>(err) << " (" << mz_zip_get_error_string(err) << ")" << std::endl;
        return false;
    }
    
    int num_files = mz_zip_reader_get_num_files(&zip_archive);
    std::cout << "[NativeDex] Bootstrap: ZIP contains " << num_files << " files." << std::endl;
    
    std::vector<uint8_t> manifestData;
    for (int i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
            continue;
        }
        
        std::string filename = file_stat.m_filename;
        if (filename == "AndroidManifest.xml") {
            manifestData.resize(file_stat.m_uncomp_size);
            if (mz_zip_reader_extract_to_mem(&zip_archive, i, manifestData.data(), manifestData.size(), 0)) {
                std::cout << "[NativeDex] Bootstrap: Extracted AndroidManifest.xml ("
                          << manifestData.size() << " bytes)." << std::endl;
            } else {
                mz_zip_error err = mz_zip_get_last_error(&zip_archive);
                std::cerr << "[NativeDex] Bootstrap: Failed to extract AndroidManifest.xml. "
                          << "miniz error=" << static_cast<int>(err) << " (" << mz_zip_get_error_string(err) << ")" << std::endl;
                manifestData.clear();
            }
        }
        
        // Check if filename matches "classes*.dex"
        if (filename.find("classes") == 0 && filename.rfind(".dex") == filename.length() - 4) {
            std::vector<uint8_t> dexData(file_stat.m_uncomp_size);
            if (mz_zip_reader_extract_to_mem(&zip_archive, i, dexData.data(), dexData.size(), 0)) {
                std::cout << "[NativeDex] Bootstrap: Extracted " << filename
                          << " (" << dexData.size() << " bytes)." << std::endl;
                result.dexDatas.push_back(std::move(dexData));
            } else {
                mz_zip_error err = mz_zip_get_last_error(&zip_archive);
                std::cerr << "[NativeDex] Bootstrap: Failed to extract " << filename
                          << ". miniz error=" << static_cast<int>(err) << " (" << mz_zip_get_error_string(err) << ")" << std::endl;
            }
        }
    }
    
    mz_zip_reader_end(&zip_archive);
    
    if (!manifestData.empty()) {
        if (!parseManifest(result, manifestData)) {
            std::cerr << "[NativeDex] Bootstrap: Warning: failed to parse AndroidManifest.xml." << std::endl;
        }
    } else {
        std::cout << "[NativeDex] Bootstrap: No AndroidManifest.xml found in the archive." << std::endl;
    }
    
    if (result.dexDatas.empty()) {
        std::cerr << "[NativeDex] Bootstrap: No classes*.dex entries found in the archive." << std::endl;
    }
    
    return !result.dexDatas.empty();
}

} // namespace NativeDex::Launcher