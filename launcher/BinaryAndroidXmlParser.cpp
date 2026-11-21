#include "BinaryAndroidXmlParser.h"
#include <cstdio>
#include <cstring>
#include <cassert>
#include <iostream>
#include <sstream>

namespace NativeDex::Launcher {

static constexpr uint16_t RES_STRING_POOL_TYPE = 0x0001;
static constexpr uint16_t RES_XML_START_NAMESPACE_TYPE = 0x0100;
static constexpr uint16_t RES_XML_END_NAMESPACE_TYPE = 0x0101;
static constexpr uint16_t RES_XML_START_ELEMENT_TYPE = 0x0102;
static constexpr uint16_t RES_XML_END_ELEMENT_TYPE = 0x0103;
static constexpr uint16_t RES_XML_CDATA_TYPE = 0x0104;

static constexpr uint32_t ATTR_TYPE_STRING = 3;
static constexpr uint32_t ATTR_TYPE_REFERENCE = 1;
static constexpr uint32_t ATTR_TYPE_INT_HEX = 17;
static constexpr uint32_t ATTR_TYPE_INT_BOOLEAN = 18;
static constexpr uint32_t ATTR_TYPE_INT_DEC = 16;

BinaryAndroidXmlParser::BinaryAndroidXmlParser(std::string_view data)
    : data_(data) {
}

bool BinaryAndroidXmlParser::parse() {
    if (data_.size() < 8) {
        error_ = "Binary XML too small";
        return false;
    }

    uint16_t chunkType = readU16();
    uint16_t headerSize = readU16();
    readU32(); // chunk size

    if (chunkType != 0x0003 && chunkType != RES_STRING_POOL_TYPE) {
        error_ = "Not a binary Android XML file";
        return false;
    }

    if (chunkType == 0x0003) {
        seek(headerSize);
    } else {
        seek(0);
    }

    if (!parseStringPool()) return false;
    if (!parseXml()) return false;
    return true;
}

const std::string& BinaryAndroidXmlParser::errorMessage() const noexcept {
    return error_;
}

const std::vector<XmlNode>& BinaryAndroidXmlParser::roots() const noexcept {
    return roots_;
}

std::optional<ManifestInfo> BinaryAndroidXmlParser::parseManifest(std::string_view data) {
    BinaryAndroidXmlParser parser(data);
    if (!parser.parse()) return std::nullopt;

    ManifestInfo info;
    for (auto& root : parser.roots_) {
        if (root.name == "manifest") {
            for (auto& attr : root.attributes) {
                if (attr.name == "package") {
                    info.packageName = attr.value;
                }
            }
            for (auto& child : root.children) {
                if (child.name == "application") {
                    for (auto& attr : child.attributes) {
                        if (attr.name == "android:label") {
                            info.label = attr.value;
                        }
                    }
                    for (auto& activity : child.children) {
                        if (activity.name == "activity") {
                            bool hasMain = false;
                            bool hasLauncher = false;
                            std::string activityName;
                            for (auto& attr : activity.attributes) {
                                if (attr.name == "android:name") {
                                    activityName = attr.value;
                                }
                            }
                            for (auto& intent : activity.children) {
                                if (intent.name == "intent-filter") {
                                    bool main = false;
                                    bool launcher = false;
                                    for (auto& item : intent.children) {
                                        if (item.name == "action") {
                                            for (auto& attr : item.attributes) {
                                                if (attr.name == "android:name" && attr.value == "android.intent.action.MAIN") {
                                                    main = true;
                                                }
                                            }
                                        }
                                        if (item.name == "category") {
                                            for (auto& attr : item.attributes) {
                                                if (attr.name == "android:name" && attr.value == "android.intent.category.LAUNCHER") {
                                                    launcher = true;
                                                }
                                            }
                                        }
                                    }
                                    if (main && launcher) {
                                        hasMain = true;
                                        hasLauncher = true;
                                        break;
                                    }
                                }
                            }
                            if (hasMain && hasLauncher && !activityName.empty()) {
                                if (activityName.rfind('.', 0) == 0) {
                                    activityName = info.packageName + activityName;
                                }
                                info.launcherActivity = activityName;
                                return info;
                            }
                        }
                    }
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<XmlNode> BinaryAndroidXmlParser::parseLayout(std::string_view data) {
    BinaryAndroidXmlParser parser(data);
    if (!parser.parse()) return std::nullopt;
    if (!parser.roots_.empty()) return parser.roots_[0];
    return std::nullopt;
}

bool BinaryAndroidXmlParser::parseStringPool() {
    const size_t start = tell();
    uint16_t chunkType = readU16();
    if (chunkType != RES_STRING_POOL_TYPE) {
        error_ = "Expected string pool";
        return false;
    }
    readU16(); // header size
    uint32_t chunkSize = readU32();
    uint32_t stringCount = readU32();
    uint32_t styleCount = readU32();
    uint32_t flags = readU32();
    uint32_t stringsStart = readU32();
    uint32_t stylesStart = readU32();
    utf8Strings_ = (flags & 0x00000100) != 0;

    std::vector<uint32_t> offsets(stringCount);
    for (uint32_t i = 0; i < stringCount; ++i) {
        offsets[i] = readU32();
    }

    size_t poolPos = start + stringsStart;
    for (uint32_t i = 0; i < stringCount; ++i) {
        seek(poolPos + offsets[i]);
        std::string value = utf8Strings_ ? readUtf8String() : readUtf16String();
        stringPool_.push_back(std::move(value));
    }

    seek(start + chunkSize);
    return true;
}

bool BinaryAndroidXmlParser::parseXml() {
    while (tell() + 8 <= data_.size()) {
        size_t nodeStart = tell();
        uint16_t chunkType = readU16();
        readU16(); // header size
        uint32_t chunkSize = readU32();
        if (chunkType == RES_XML_START_ELEMENT_TYPE) {
            // Layout per AOSP ResXMLTree_node:
            //   header (8): type(2) + headerSize(2) + size(4)
            //   body: lineNumber(4) + comment(4) + ns(4) + name(4)
            //         attributeStart(2) + attributeSize(2) + attributeCount(2)
            //         idIndex(2) + classIndex(2) + styleIndex(2)
            //         attributes: 20 bytes each (ns(4) + name(4) + rawValue(4) + typedValue(8))
            // Total body = 16 + 12 + 20*count.
            readU32(); // lineNumber
            readU32(); // comment
            uint32_t nsIdx = readU32();
            uint32_t nameIdx = readU32();
            readU16(); // attributeStart
            readU16(); // attributeSize
            uint16_t attributeCount = readU16();
            readU16(); // idIndex (classAttribute)
            readU16(); // classIndex
            readU16(); // styleIndex
            (void)nsIdx;
            XmlNode node;
            node.name = readStringFromPool(nameIdx);
            for (uint32_t i = 0; i < attributeCount; ++i) {
                readU32(); // attr ns (string pool index)
                uint32_t attrName = readU32();
                readU32(); // rawValue.string_idx (or 0xFFFFFFFF)
                readU32(); // typedValue.size + res0 + type
                readU32(); // typedValue.data
                XmlAttribute attr;
                attr.name = readStringFromPool(attrName);
                node.attributes.push_back(std::move(attr));
            }
            if (!nodeStack_.empty()) {
                nodeStack_.back()->children.push_back(std::move(node));
                nodeStack_.push_back(&nodeStack_.back()->children.back());
            } else {
                roots_.push_back(std::move(node));
                nodeStack_.push_back(&roots_.back());
            }
            // Force-seek to the end of this chunk to avoid losing alignment.
            seek(nodeStart + chunkSize);
        } else if (chunkType == RES_XML_END_ELEMENT_TYPE) {
            // body: lineNumber(4) + comment(4) + ns(4) + name(4) = 16 bytes
            // Already read 8-byte header.
            readU32(); // lineNumber
            readU32(); // comment
            readU32(); // nsIdx
            readU32(); // nameIdx
            if (!nodeStack_.empty()) nodeStack_.pop_back();
            seek(nodeStart + chunkSize);
        } else if (chunkType == RES_XML_START_NAMESPACE_TYPE
                || chunkType == RES_XML_END_NAMESPACE_TYPE) {
            // body: lineNumber(4) + comment(4) + prefix(4) + uri(4) = 16 bytes
            readU32(); // lineNumber
            readU32(); // comment
            readU32(); // prefix
            readU32(); // uri
            seek(nodeStart + chunkSize);
        } else {
            // Skip unsupported chunk.
            seek(nodeStart + chunkSize);
        }
    }
    return true;
}

std::string BinaryAndroidXmlParser::readStringFromPool(uint32_t index) {
    if (index < stringPool_.size()) return stringPool_[index];
    return {};
}

std::optional<std::string> BinaryAndroidXmlParser::readAttributeValue(uint32_t rawValue, uint8_t dataType, uint32_t data) {
    if (rawValue != 0xFFFFFFFFu) {
        return readStringFromPool(rawValue);
    }
    switch (dataType) {
        case ATTR_TYPE_STRING:
            return readStringFromPool(data);
        case ATTR_TYPE_INT_BOOLEAN:
            return data ? std::string("true") : std::string("false");
        case ATTR_TYPE_INT_DEC: {
            return std::to_string(data);
        }
        case ATTR_TYPE_INT_HEX: {
            std::ostringstream oss;
            oss << "0x" << std::hex << data;
            return oss.str();
        }
        case 0x01: {   // reference
            char buf[24];
            std::snprintf(buf, sizeof buf, "@0x%08x", data);
            return std::string(buf);
        }
        case 0x02: {   // attribute reference
            char buf[24];
            std::snprintf(buf, sizeof buf, "?0x%08x", data);
            return std::string(buf);
        }
        case 0x04: {   // float
            float f;
            std::memcpy(&f, &data, sizeof f);
            return std::to_string(f);
        }
        case 0x05: {   // dimension: 22 bits mantissa/radix + 4 bits unit
            static const char* units[] = {"px", "dp", "sp", "pt", "in", "mm"};
            static const float radix[] = {1.0f / (1 << 8), 1.0f / (1 << 15), 1.0f / (1 << 23), 1.0f / (1u << 31) };
            float v = static_cast<float>(static_cast<int32_t>(data & 0xFFFFFF00u)) * radix[(data >> 4) & 3];
            unsigned unit = data & 0xF;
            return std::to_string(v) + (unit < 6 ? units[unit] : "px");
        }
        case 0x06: {   // fraction
            static const float radix[] = {1.0f / (1 << 8), 1.0f / (1 << 15), 1.0f / (1 << 23), 1.0f / (1u << 31) };
            return std::to_string(static_cast<float>(static_cast<int32_t>(data & 0xFFFFFF00u)) * radix[(data >> 4) & 3] * 100.0f) + "%";
        }
        case 0x1C: case 0x1D: case 0x1E: case 0x1F: {   // colors
            char buf[16];
            std::snprintf(buf, sizeof buf, "#%08x", data);
            return std::string(buf);
        }
        default:
            return std::nullopt;
    }
}

std::string BinaryAndroidXmlParser::readUtf8String() {
    readLength8();
    uint32_t byteCount = readLength8();
    std::string result(reinterpret_cast<const char*>(data_.data() + tell()), byteCount);
    seek(tell() + byteCount + 1);
    return result;
}

std::string BinaryAndroidXmlParser::readUtf16String() {
    uint32_t length = readLength16();
    std::u16string result;
    result.reserve(length);
    for (uint32_t i = 0; i < length; ++i) {
        uint16_t ch = readU16();
        result.push_back(static_cast<char16_t>(ch));
    }
    seek(tell() + 2);
    std::string utf8;
    utf8.reserve(length);
    for (char16_t ch : result) utf8.push_back(static_cast<char>(ch));
    return utf8;
}

uint32_t BinaryAndroidXmlParser::readLength8() {
    uint8_t len = readU8();
    if (len & 0x80u) {
        uint8_t high = readU8();
        return ((len & 0x7Fu) << 7) | high;
    }
    return len;
}

uint32_t BinaryAndroidXmlParser::readLength16() {
    uint16_t len = readU16();
    if (len & 0x8000u) {
        uint16_t high = readU16();
        return ((len & 0x7FFFu) << 15) | high;
    }
    return len;
}

uint8_t BinaryAndroidXmlParser::readU8() {
    assert(tell() + 1 <= data_.size());
    return static_cast<uint8_t>(data_[offset_++]);
}

uint16_t BinaryAndroidXmlParser::readU16() {
    assert(tell() + 2 <= data_.size());
    uint16_t value = static_cast<uint8_t>(data_[offset_]) | (static_cast<uint8_t>(data_[offset_ + 1]) << 8);
    offset_ += 2;
    return value;
}

uint32_t BinaryAndroidXmlParser::readU32() {
    assert(tell() + 4 <= data_.size());
    uint32_t value = static_cast<uint8_t>(data_[offset_]) |
                     (static_cast<uint8_t>(data_[offset_ + 1]) << 8) |
                     (static_cast<uint8_t>(data_[offset_ + 2]) << 16) |
                     (static_cast<uint8_t>(data_[offset_ + 3]) << 24);
    offset_ += 4;
    return value;
}

void BinaryAndroidXmlParser::seek(size_t position) {
    offset_ = position;
}

size_t BinaryAndroidXmlParser::tell() const noexcept {
    return offset_;
}

} // namespace NativeDex::Launcher
