#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <chrono>

namespace NativeDex::Platform::Windows {

class Win32FileSystem {
public:
    using Path = std::filesystem::path;
    
    struct FileInfo {
        Path path;
        uint64_t size = 0;
        std::chrono::system_clock::time_point lastWriteTime;
        std::chrono::system_clock::time_point creationTime;
        bool isDirectory = false;
        bool isHidden = false;
        bool isReadOnly = false;
    };
    
    struct FileHandle {
        HANDLE handle = INVALID_HANDLE_VALUE;
        Path path;
        bool readOnly = false;
        ~FileHandle() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    };
    
    // File operations
    static std::unique_ptr<FileHandle> openFile(const Path& path, bool readOnly = true);
    static bool createFile(const Path& path);
    static bool deleteFile(const Path& path);
    static bool copyFile(const Path& src, const Path& dst, bool overwrite = false);
    static bool moveFile(const Path& src, const Path& dst);
    static bool exists(const Path& path);
    static FileInfo getFileInfo(const Path& path);
    
    // Directory operations
    static bool createDirectory(const Path& path);
    static bool createDirectories(const Path& path);
    static bool removeDirectory(const Path& path);
    static bool removeAll(const Path& path);
    static std::vector<FileInfo> listDirectory(const Path& path);
    
    // Read/Write
    static std::vector<uint8_t> readAllBytes(const Path& path);
    static std::string readAllText(const Path& path);
    static bool writeAllBytes(const Path& path, const std::vector<uint8_t>& data);
    static bool writeAllText(const Path& path, const std::string& text);
    static bool appendText(const Path& path, const std::string& text);
    
    // Stream-based operations
    class FileInputStream {
    public:
        FileInputStream(const Path& path);
        ~FileInputStream();
        
        size_t read(void* buffer, size_t size);
        bool isOpen() const { return file_ != nullptr; }
        bool eof() const { return eof_; }
        uint64_t getPosition() const { return position_; }
        void seek(uint64_t pos);
        
    private:
        std::unique_ptr<FileHandle> file_;
        uint64_t position_ = 0;
        bool eof_ = false;
    };
    
    class FileOutputStream {
    public:
        FileOutputStream(const Path& path, bool append = false);
        ~FileOutputStream();
        
        size_t write(const void* buffer, size_t size);
        void flush();
        bool isOpen() const { return file_ != nullptr; }
        uint64_t getPosition() const { return position_; }
        
    private:
        std::unique_ptr<FileHandle> file_;
        uint64_t position_ = 0;
    };
    
    // Asset loading (for APK assets)
    class AssetManager {
    public:
        explicit AssetManager(const Path& apkPath);
        ~AssetManager();
        
        std::vector<uint8_t> loadAsset(const std::string& assetPath);
        bool hasAsset(const std::string& assetPath);
        std::vector<std::string> listAssets(const std::string& prefix);
        
    private:
        Path apkPath_;
        HANDLE apkHandle_ = INVALID_HANDLE_VALUE;
        // Would use miniz or similar for ZIP reading
    };
    
    // Path utilities
    static Path getCurrentDirectory();
    static void setCurrentDirectory(const Path& path);
    static Path getTempDirectory();
    static Path getAppDataDirectory(const std::string& appName);
    static Path getCacheDirectory(const std::string& appName);
    static Path getDocumentsDirectory();
    static Path combine(const Path& base, const Path& relative);
    static Path getFileName(const Path& path);
    static Path getParentPath(const Path& path);
    static std::string getExtension(const Path& path);
    static std::string getStem(const Path& path);
    
    // File attributes
    static bool isHidden(const Path& path);
    static void setHidden(const Path& path, bool hidden);
    static bool isReadOnly(const Path& path);
    static void setReadOnly(const Path& path, bool readOnly);
    static uint32_t getAttributes(const Path& path);
    static void setAttributes(const Path& path, uint32_t attributes);
};

} // namespace NativeDex::Platform::Windows