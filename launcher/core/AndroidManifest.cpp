#include "AndroidManifest.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace NativeDex::Launcher {

namespace {

constexpr uint16_t RES_STRING_POOL_TYPE       = 0x0001;
constexpr uint16_t RES_XML_TYPE               = 0x0003;
constexpr uint16_t RES_XML_START_ELEMENT_TYPE = 0x0102;
constexpr uint16_t RES_XML_END_ELEMENT_TYPE   = 0x0103;

constexpr uint8_t TYPE_STRING = 0x03;

constexpr uint32_t NO_NAMESPACE = 0xFFFFFFFF;

constexpr const char* ANDROID_NS = "http://schemas.android.com/apk/res/android";
constexpr const char* ACTION_MAIN = "android.intent.action.MAIN";
constexpr const char* CATEGORY_LAUNCHER = "android.intent.category.LAUNCHER";

} // namespace

BinaryXmlParser::BinaryXmlParser(const std::vector<uint8_t>& data) : data_(data) {}

bool BinaryXmlParser::parse(AndroidManifest& out) {
    if (data_.size() < 8) return false;

    const uint16_t type = readU16(0);
    uint32_t totalSize = readU32(4);
    if (type != RES_XML_TYPE && type != RES_STRING_POOL_TYPE) return false;
    if (totalSize < 8) return false;
    if (totalSize > data_.size()) totalSize = static_cast<uint32_t>(data_.size());

    strings_.clear();
    elementStack_.clear();
    activityStack_.clear();
    filterAction_.clear();
    filterCategory_.clear();

    size_t off = 8;
    while (off + 8 <= totalSize) {
        const uint16_t chunkType = readU16(off);
        const uint32_t chunkSize = readU32(off + 4);
        if (chunkSize < 8) break;
        if (off + chunkSize > totalSize) break;

        switch (chunkType) {
            case RES_STRING_POOL_TYPE:
                readStringPool(off, chunkSize);
                break;
            case RES_XML_START_ELEMENT_TYPE:
                parseStartElement(off, out);
                break;
            case RES_XML_END_ELEMENT_TYPE:
                parseEndElement(off, out);
                break;
            default:
                break;
        }

        off += chunkSize;
    }

    return true;
}

bool BinaryXmlParser::readStringPool(size_t base, size_t chunkSize) {
    if (base + 28 > data_.size()) return false;

    const uint32_t stringCount = readU32(base + 8);
    const uint32_t flags = readU32(base + 16);
    const uint32_t stringsStart = readU32(base + 20);
    const bool isUtf8 = (flags & 0x100) != 0;

    const size_t dataLimit = std::min(data_.size(), base + chunkSize);

    strings_.clear();
    strings_.reserve(stringCount);

    for (uint32_t i = 0; i < stringCount; ++i) {
        const uint32_t off = readU32(base + 28 + i * 4);
        size_t pos = base + stringsStart + off;
        std::string str;

        if (isUtf8) {
            if (pos >= dataLimit) break;

            uint8_t b = readU8(pos);
            pos += 1;
            uint32_t utf16Len = b;
            if (b & 0x80) {
                utf16Len = static_cast<uint32_t>(((b & 0x7F) << 8) | readU8(pos));
                pos += 1;
            }

            if (pos >= dataLimit) break;
            b = readU8(pos);
            pos += 1;
            uint32_t byteLen = b;
            if (b & 0x80) {
                byteLen = static_cast<uint32_t>(((b & 0x7F) << 8) | readU8(pos));
                pos += 1;
            }

            if (byteLen > dataLimit - pos) byteLen = static_cast<uint32_t>(dataLimit - pos);
            str.assign(data_.begin() + static_cast<std::ptrdiff_t>(pos),
                       data_.begin() + static_cast<std::ptrdiff_t>(pos + byteLen));
            while (!str.empty() && str.back() == '\0') str.pop_back();
        } else {
            if (pos + 2 > dataLimit) break;
            const uint32_t utf16Len = readU16(pos);
            pos += 2;

            for (uint32_t j = 0; j < utf16Len; ++j) {
                if (pos + 2 > dataLimit) break;
                const uint16_t unit = readU16(pos);
                pos += 2;

                if (unit >= 0xD800 && unit <= 0xDBFF) {
                    if (pos + 2 <= dataLimit) {
                        const uint16_t low = readU16(pos);
                        pos += 2;
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            const uint32_t cp = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                            str += static_cast<char>(0xF0 | (cp >> 18));
                            str += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                            str += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            str += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                    }
                } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                    // Lone low surrogate; drop.
                } else if (unit < 0x80) {
                    str += static_cast<char>(unit);
                } else if (unit < 0x800) {
                    str += static_cast<char>(0xC0 | (unit >> 6));
                    str += static_cast<char>(0x80 | (unit & 0x3F));
                } else {
                    str += static_cast<char>(0xE0 | (unit >> 12));
                    str += static_cast<char>(0x80 | ((unit >> 6) & 0x3F));
                    str += static_cast<char>(0x80 | (unit & 0x3F));
                }
            }
        }

        strings_.push_back(std::move(str));
    }

    return !strings_.empty();
}

void BinaryXmlParser::parseStartElement(size_t base, AndroidManifest& out) {
    const std::string* elemName = getString(readU32(base + 20));
    if (!elemName) return;

    elementStack_.push_back(*elemName);

    const uint16_t headerSize = readU16(base + 2);
    const uint16_t attributeStart = readU16(base + 24);
    const uint16_t attributeSize = readU16(base + 26);
    const uint16_t attributeCount = readU16(base + 28);

    const size_t attrBase = base + (headerSize >= 16 ? headerSize : 16) + attributeStart;

    auto getAttr = [&](const std::string& ns, const std::string& attrName) -> std::string {
        for (uint16_t i = 0; i < attributeCount; ++i) {
            const size_t pos = attrBase + static_cast<size_t>(i) * attributeSize;
            const uint32_t aNs = readU32(pos);
            const uint32_t aName = readU32(pos + 4);
            const uint32_t aRaw = readU32(pos + 8);
            const uint8_t dataType = readU8(pos + 15);
            const uint32_t data = readU32(pos + 16);

            const std::string* nameStr = getString(aName);
            if (!nameStr || *nameStr != attrName) continue;

            const std::string* nsStr = (aNs != NO_NAMESPACE) ? getString(aNs) : nullptr;
            std::string nsVal = nsStr ? *nsStr : std::string();

            if (ns.empty()) {
                if (!nsVal.empty()) continue;
            } else {
                if (nsVal != ns) continue;
            }

            if (dataType == TYPE_STRING) {
                const std::string* s = getString(data);
                if (s) return *s;
            } else {
                const std::string* s = getString(aRaw);
                if (s) return *s;
            }
            return std::string();
        }
        return std::string();
    };

    if (*elemName == "manifest") {
        if (out.packageName.empty()) {
            const std::string pkg = getAttr(std::string(), "package");
            if (!pkg.empty()) out.packageName = pkg;
        }
    } else if (*elemName == "application") {
        if (out.appLabel.empty()) {
            const std::string label = getAttr(ANDROID_NS, "label");
            if (!label.empty()) out.appLabel = label;
        }
        if (out.applicationClass.empty()) {
            const std::string appName = getAttr(ANDROID_NS, "name");
            if (!appName.empty()) out.applicationClass = resolveActivityName(appName, out.packageName);
        }
    } else if (*elemName == "activity" || *elemName == "activity-alias") {
        const std::string actName = getAttr(ANDROID_NS, "name");
        ManifestActivity act;
        act.name = resolveActivityName(actName, out.packageName);
        activityStack_.push_back(std::move(act));
        filterAction_.clear();
        filterCategory_.clear();
    } else if (*elemName == "intent-filter") {
        filterAction_.clear();
        filterCategory_.clear();
    } else if (*elemName == "action") {
        const std::string action = getAttr(ANDROID_NS, "name");
        if (!action.empty()) filterAction_ = action;
    } else if (*elemName == "category") {
        const std::string category = getAttr(ANDROID_NS, "name");
        if (!category.empty()) filterCategory_ = category;
    }
}

void BinaryXmlParser::parseEndElement(size_t base, AndroidManifest& out) {
    const std::string* elemName = getString(readU32(base + 20));
    if (!elementStack_.empty()) elementStack_.pop_back();
    if (!elemName) return;

    if (*elemName == "activity" || *elemName == "activity-alias") {
        if (!activityStack_.empty()) {
            ManifestActivity& act = activityStack_.back();
            act.isLauncher = (filterAction_ == ACTION_MAIN && filterCategory_ == CATEGORY_LAUNCHER);
            const std::string name = act.name;
            const bool isLauncher = act.isLauncher;
            if (!name.empty()) {
                out.activities.push_back(std::move(act));
                if (isLauncher && out.launcherActivity.empty()) {
                    out.launcherActivity = name;
                }
            }
            activityStack_.pop_back();
        }
        filterAction_.clear();
        filterCategory_.clear();
    }
}

uint8_t BinaryXmlParser::readU8(size_t off) const {
    return off < data_.size() ? data_[off] : 0;
}

uint16_t BinaryXmlParser::readU16(size_t off) const {
    if (off + 1 >= data_.size()) return 0;
    return static_cast<uint16_t>(data_[off] | (data_[off + 1] << 8));
}

uint32_t BinaryXmlParser::readU32(size_t off) const {
    if (off + 3 >= data_.size()) return 0;
    return static_cast<uint32_t>(data_[off]) |
           (static_cast<uint32_t>(data_[off + 1]) << 8) |
           (static_cast<uint32_t>(data_[off + 2]) << 16) |
           (static_cast<uint32_t>(data_[off + 3]) << 24);
}

const std::string* BinaryXmlParser::getString(uint32_t idx) const {
    return idx < strings_.size() ? &strings_[idx] : nullptr;
}

std::string BinaryXmlParser::resolveActivityName(const std::string& name, const std::string& package) {
    if (name.empty()) return name;
    if (name[0] == '.') {
        return package.empty() ? name.substr(1) : package + name;
    }
    if (name.find('.') != std::string::npos) return name;
    return package.empty() ? name : package + "." + name;
}

} // namespace NativeDex::Launcher
