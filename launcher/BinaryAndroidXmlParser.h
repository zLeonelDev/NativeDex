#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace NativeDex::Launcher {

struct XmlAttribute {
    std::string name;
    std::string value;
};

struct XmlNode {
    std::string name;
    std::vector<XmlAttribute> attributes;
    std::vector<XmlNode> children;
};

struct ManifestInfo {
    std::string packageName;
    std::string launcherActivity;
    std::string label;
};

class BinaryAndroidXmlParser {
public:
    explicit BinaryAndroidXmlParser(std::string_view data);

    bool parse();
    const std::string& errorMessage() const noexcept;
    const std::vector<XmlNode>& roots() const noexcept;

    static std::optional<ManifestInfo> parseManifest(std::string_view data);
    static std::optional<XmlNode> parseLayout(std::string_view data);

private:
    bool parseStringPool();
    bool parseXml();
    std::string readStringFromPool(uint32_t index);
    std::optional<std::string> readAttributeValue(uint32_t rawValue, uint8_t dataType, uint32_t data);
    std::string readUtf8String();
    std::string readUtf16String();
    uint32_t readLength8();
    uint32_t readLength16();

    std::vector<std::string> stringPool_;
    // Pointer-stack of currently-open XML elements. Using pointers (not
    // copies) avoids the classic `push_back(copies back())` invalidation
    // bug when child vectors reallocate.
    std::vector<XmlNode*> nodeStack_;
    std::vector<XmlNode> roots_;
    std::string error_;

    std::string_view data_;
    size_t offset_ = 0;
    bool utf8Strings_ = false;

    uint8_t readU8();
    uint16_t readU16();
    uint32_t readU32();
    void seek(size_t position);
    size_t tell() const noexcept;
};

} // namespace NativeDex::Launcher
