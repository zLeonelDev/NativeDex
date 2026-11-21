#include "Win32FileSystem.h"
#include <fstream>
#include <iostream>
#include <shlobj.h>

namespace NativeDex::Platform::Windows {

std::unique_ptr<Win32FileSystem::FileHandle> Win32FileSystem::openFile(const Path& path, bool readOnly) {
    auto handle = std::make_unique<FileHandle>();
    handle->path = path;
    handle->readOnly = readOnly;
    
    DWORD access = readOnly ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    DWORD share = readOnly ? FILE_SHARE_READ : 0;
    DWORD disposition = readOnly ? OPEN_EXISTING : OPEN_ALWAYS;
    
    handle->handle = CreateFileW(
        path.wstring().c_str(),
        access,
        share,
        nullptr,
        disposition,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    
    if (handle->handle == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    
    return handle;
}

bool Win32FileSystem::createFile(const Path& path) {
    HANDLE h = CreateFileW(
        path.wstring().c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return true;
    }
    return false;
}

bool Win32FileSystem::deleteFile(const Path& path) {
    return DeleteFileW(path.wstring().c_str()) != FALSE;
}

bool Win32FileSystem::copyFile(const Path& src, const Path& dst, bool overwrite) {
    return CopyFileW(src.wstring().c_str(), dst.wstring().c_str(), !overwrite) != FALSE;
}

bool Win32FileSystem::moveFile(const Path& src, const Path& dst) {
    return MoveFileExW(src.wstring().c_str(), dst.wstring().c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

bool Win32FileSystem::exists(const Path& path) {
    return std::filesystem::exists(path);
}

Win32FileSystem::FileInfo Win32FileSystem::getFileInfo(const Path& path) {
    FileInfo info;
    info.path = path;
    
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExW(path.wstring().c_str(), GetFileExInfoStandard, &data)) {
        info.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        info.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        info.isHidden = (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        info.isReadOnly = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        
        // Convert FILETIME to system_clock::time_point
        auto fileTimeToTimePoint = [](const FILETIME& ft) {
            ULARGE_INTEGER ul;
            ul.LowPart = ft.dwLowDateTime;
            ul.HighPart = ft.dwHighDateTime;
            // FILETIME is 100-nanosecond intervals since Jan 1, 1601
            // system_clock epoch is Jan 1, 1970
            const uint64_t EPOCH_DIFF = 116444736000000000ULL; // 100ns intervals
            return std::chrono::system_clock::time_point(
                std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    std::chrono::nanoseconds((ul.QuadPart - EPOCH_DIFF) * 100)
                )
            );
        };
        
        info.lastWriteTime = fileTimeToTimePoint(data.ftLastWriteTime);
        info.creationTime = fileTimeToTimePoint(data.ftCreationTime);
    }
    
    return info;
}

bool Win32FileSystem::createDirectory(const Path& path) {
    return CreateDirectoryW(path.wstring().c_str(), nullptr) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool Win32FileSystem::createDirectories(const Path& path) {
    return std::filesystem::create_directories(path);
}

bool Win32FileSystem::removeDirectory(const Path& path) {
    return RemoveDirectoryW(path.wstring().c_str()) != FALSE;
}

bool Win32FileSystem::removeAll(const Path& path) {
    try {
        std::filesystem::remove_all(path);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<Win32FileSystem::FileInfo> Win32FileSystem::listDirectory(const Path& path) {
    std::vector<FileInfo> result;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            result.push_back(getFileInfo(entry.path()));
        }
    } catch (...) {}
    return result;
}

std::vector<uint8_t> Win32FileSystem::readAllBytes(const Path& path) {
    auto handle = openFile(path, true);
    if (!handle) return {};
    
    LARGE_INTEGER size;
    if (!GetFileSizeEx(handle->handle, &size)) return {};
    
    std::vector<uint8_t> data(size.QuadPart);
    DWORD bytesRead = 0;
    if (!ReadFile(handle->handle, data.data(), static_cast<DWORD>(data.size()), &bytesRead, nullptr)) {
        return {};
    }
    data.resize(bytesRead);
    return data;
}

std::string Win32FileSystem::readAllText(const Path& path) {
    auto bytes = readAllBytes(path);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool Win32FileSystem::writeAllBytes(const Path& path, const std::vector<uint8_t>& data) {
    auto handle = openFile(path, false);
    if (!handle) return false;
    
    DWORD bytesWritten = 0;
    return WriteFile(handle->handle, data.data(), static_cast<DWORD>(data.size()), &bytesWritten, nullptr) != FALSE &&
           bytesWritten == data.size();
}

bool Win32FileSystem::writeAllText(const Path& path, const std::string& text) {
    return writeAllBytes(path, std::vector<uint8_t>(text.begin(), text.end()));
}

bool Win32FileSystem::appendText(const Path& path, const std::string& text) {
    auto handle = openFile(path, false);
    if (!handle) return false;
    
    SetFilePointer(handle->handle, 0, nullptr, FILE_END);
    DWORD bytesWritten = 0;
    return WriteFile(handle->handle, text.c_str(), static_cast<DWORD>(text.size()), &bytesWritten, nullptr) != FALSE &&
           bytesWritten == text.size();
}

Win32FileSystem::FileInputStream::FileInputStream(const Path& path) {
    file_ = openFile(path, true);
    if (file_) {
        position_ = 0;
        eof_ = false;
    } else {
        eof_ = true;
    }
}

Win32FileSystem::FileInputStream::~FileInputStream() = default;

size_t Win32FileSystem::FileInputStream::read(void* buffer, size_t size) {
    if (!file_ || eof_) return 0;
    
    DWORD bytesRead = 0;
    if (!ReadFile(file_->handle, buffer, static_cast<DWORD>(size), &bytesRead, nullptr)) {
        eof_ = true;
        return 0;
    }
    
    if (bytesRead == 0) {
        eof_ = true;
    }
    
    position_ += bytesRead;
    return bytesRead;
}

void Win32FileSystem::FileInputStream::seek(uint64_t pos) {
    if (!file_) return;
    LARGE_INTEGER li;
    li.QuadPart = pos;
    SetFilePointerEx(file_->handle, li, nullptr, FILE_BEGIN);
    position_ = pos;
    eof_ = false;
}

Win32FileSystem::FileOutputStream::FileOutputStream(const Path& path, bool append) {
    file_ = openFile(path, false);
    if (file_) {
        if (append) {
            SetFilePointer(file_->handle, 0, nullptr, FILE_END);
            LARGE_INTEGER pos;
            GetFileSizeEx(file_->handle, &pos);
            position_ = pos.QuadPart;
        } else {
            position_ = 0;
        }
    }
}

Win32FileSystem::FileOutputStream::~FileOutputStream() {
    flush();
}

size_t Win32FileSystem::FileOutputStream::write(const void* buffer, size_t size) {
    if (!file_) return 0;
    
    DWORD bytesWritten = 0;
    if (!WriteFile(file_->handle, buffer, static_cast<DWORD>(size), &bytesWritten, nullptr)) {
        return 0;
    }
    
    position_ += bytesWritten;
    return bytesWritten;
}

void Win32FileSystem::FileOutputStream::flush() {
    if (file_) {
        FlushFileBuffers(file_->handle);
    }
}

Win32FileSystem::AssetManager::AssetManager(const Path& apkPath) : apkPath_(apkPath) {
    // Would open APK as ZIP archive using miniz
}

Win32FileSystem::AssetManager::~AssetManager() {
    if (apkHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(apkHandle_);
    }
}

std::vector<uint8_t> Win32FileSystem::AssetManager::loadAsset(const std::string& assetPath) {
    // Would extract from ZIP
    return {};
}

bool Win32FileSystem::AssetManager::hasAsset(const std::string& assetPath) {
    return false;
}

std::vector<std::string> Win32FileSystem::AssetManager::listAssets(const std::string& prefix) {
    return {};
}

Win32FileSystem::Path Win32FileSystem::getCurrentDirectory() {
    return std::filesystem::current_path();
}

void Win32FileSystem::setCurrentDirectory(const Path& path) {
    std::filesystem::current_path(path);
}

Win32FileSystem::Path Win32FileSystem::getTempDirectory() {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    return Path(tempPath);
}

Win32FileSystem::Path Win32FileSystem::getAppDataDirectory(const std::string& appName) {
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appDataPath))) {
        Path result(appDataPath);
        result /= std::filesystem::u8path(appName);
        return result;
    }
    return getTempDirectory() / std::filesystem::u8path(appName);
}

Win32FileSystem::Path Win32FileSystem::getCacheDirectory(const std::string& appName) {
    return getAppDataDirectory(appName) / "cache";
}

Win32FileSystem::Path Win32FileSystem::getDocumentsDirectory() {
    wchar_t docsPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docsPath))) {
        return Path(docsPath);
    }
    return getTempDirectory();
}

Win32FileSystem::Path Win32FileSystem::combine(const Path& base, const Path& relative) {
    return base / relative;
}

Win32FileSystem::Path Win32FileSystem::getFileName(const Path& path) {
    return path.filename();
}

Win32FileSystem::Path Win32FileSystem::getParentPath(const Path& path) {
    return path.parent_path();
}

std::string Win32FileSystem::getExtension(const Path& path) {
    return path.extension().string();
}

std::string Win32FileSystem::getStem(const Path& path) {
    return path.stem().string();
}

bool Win32FileSystem::isHidden(const Path& path) {
    DWORD attrs = GetFileAttributesW(path.wstring().c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_HIDDEN) != 0;
}

void Win32FileSystem::setHidden(const Path& path, bool hidden) {
    DWORD attrs = GetFileAttributesW(path.wstring().c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (hidden) attrs |= FILE_ATTRIBUTE_HIDDEN;
        else attrs &= ~FILE_ATTRIBUTE_HIDDEN;
        SetFileAttributesW(path.wstring().c_str(), attrs);
    }
}

bool Win32FileSystem::isReadOnly(const Path& path) {
    DWORD attrs = GetFileAttributesW(path.wstring().c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) != 0;
}

void Win32FileSystem::setReadOnly(const Path& path, bool readOnly) {
    DWORD attrs = GetFileAttributesW(path.wstring().c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (readOnly) attrs |= FILE_ATTRIBUTE_READONLY;
        else attrs &= ~FILE_ATTRIBUTE_READONLY;
        SetFileAttributesW(path.wstring().c_str(), attrs);
    }
}

uint32_t Win32FileSystem::getAttributes(const Path& path) {
    return GetFileAttributesW(path.wstring().c_str());
}

void Win32FileSystem::setAttributes(const Path& path, uint32_t attributes) {
    SetFileAttributesW(path.wstring().c_str(), attributes);
}

} // namespace NativeDex::Platform::Windows