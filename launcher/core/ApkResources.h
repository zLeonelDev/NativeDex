#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace NativeDex::Launcher {

// Read-only view of an APK: its files and the resource table
// (resources.arsc). Replaces the old "guess the layout by name" approach:
// resource ids (R.layout.*, R.string.*, R.id.*) are resolved through the
// real table.
class ApkResources {
public:
    static ApkResources& instance();

    // Loads the APK (zip) from memory and parses resources.arsc if present.
    bool load(std::vector<uint8_t> apkBytes);
    bool loaded() const { return loaded_; }

    // Raw file from the APK ("res/layout/foo.xml"); empty if missing.
    std::vector<uint8_t> readFile(const std::string& path) const;

    struct Value {
        bool found = false;
        uint8_t type = 0;          // Res_value dataType
        uint32_t data = 0;
        std::string str;           // for TYPE_STRING: the string / file path
    };

    // Resolves a resource id, following references (up to a few hops).
    Value resolve(uint32_t resId) const;

    std::string typeName(uint32_t resId) const;     // "layout", "string", "id", ...
    std::string entryName(uint32_t resId) const;    // e.g. "main_layout"
    std::string getString(uint32_t resId) const;    // "" if not a string
    // R.<type>.<name> -> id, 0 if unknown.
    uint32_t findId(const std::string& type, const std::string& name) const;
    // File path for a file-backed resource (layout, drawable xml, ...).
    std::string filePath(uint32_t resId) const;

private:
    ApkResources() = default;
    struct Entry {
        std::string name;
        std::vector<Value> values;   // [0] is the default configuration
    };
    bool parseTable(const std::vector<uint8_t>& arsc);

    bool loaded_ = false;
    std::vector<uint8_t> apk_;
    struct ZipHolder;
    std::shared_ptr<ZipHolder> zip_;
    std::unordered_map<uint32_t, Entry> entries_;
    std::unordered_map<uint32_t, std::string> typeNames_;     // (pkg<<8|type) -> name
    std::unordered_map<std::string, uint32_t> byName_;        // "type/name" -> id
};

} // namespace NativeDex::Launcher
