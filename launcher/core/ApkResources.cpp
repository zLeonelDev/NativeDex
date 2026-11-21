#include "ApkResources.h"

#include <cstring>
#include <iostream>

#include "../../third_party/miniz/miniz.h"

namespace NativeDex::Launcher {

struct ApkResources::ZipHolder {
    mz_zip_archive zip;
    bool open = false;
    ZipHolder() { std::memset(&zip, 0, sizeof(zip)); }
    ~ZipHolder() { if (open) mz_zip_reader_end(&zip); }
};

ApkResources& ApkResources::instance() {
    static ApkResources inst;
    return inst;
}

namespace {

struct Reader {
    const uint8_t* p;
    size_t n;
    bool ok(size_t off, size_t len) const { return off + len <= n; }
    uint8_t u8(size_t o) const { return ok(o, 1) ? p[o] : 0; }
    uint16_t u16(size_t o) const { return ok(o, 2) ? static_cast<uint16_t>(p[o] | (p[o + 1] << 8)) : 0; }
    uint32_t u32(size_t o) const {
        return ok(o, 4) ? (static_cast<uint32_t>(p[o]) | (static_cast<uint32_t>(p[o + 1]) << 8) |
                           (static_cast<uint32_t>(p[o + 2]) << 16) | (static_cast<uint32_t>(p[o + 3]) << 24)) : 0;
    }
};

std::string utf16ToUtf8(const uint16_t* s, size_t len) {
    std::string out;
    for (size_t i = 0; i < len; ++i) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < len && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            ++i;
        }
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) { out.push_back(static_cast<char>(0xC0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
        else if (c < 0x10000) { out.push_back(static_cast<char>(0xE0 | (c >> 12))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
        else { out.push_back(static_cast<char>(0xF0 | (c >> 18))); out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
    }
    return out;
}

// Parses a ResStringPool chunk located at `base`.
std::vector<std::string> readStringPool(const Reader& r, size_t base) {
    std::vector<std::string> out;
    uint32_t count = r.u32(base + 8);
    uint32_t flags = r.u32(base + 16);
    uint32_t stringsStart = r.u32(base + 20);
    uint16_t headerSize = r.u16(base + 2);
    bool utf8 = (flags & 0x100) != 0;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t off = r.u32(base + headerSize + i * 4);
        size_t pos = base + stringsStart + off;
        if (!r.ok(pos, 2)) { out.emplace_back(); continue; }
        if (utf8) {
            size_t q = pos;
            uint32_t chars = r.u8(q++);
            if (chars & 0x80) chars = ((chars & 0x7F) << 8) | r.u8(q++);
            uint32_t bytes = r.u8(q++);
            if (bytes & 0x80) bytes = ((bytes & 0x7F) << 8) | r.u8(q++);
            if (!r.ok(q, bytes)) { out.emplace_back(); continue; }
            out.emplace_back(reinterpret_cast<const char*>(r.p + q), bytes);
        } else {
            size_t q = pos;
            uint32_t len = r.u16(q); q += 2;
            if (len & 0x8000) { len = ((len & 0x7FFF) << 16) | r.u16(q); q += 2; }
            if (!r.ok(q, static_cast<size_t>(len) * 2)) { out.emplace_back(); continue; }
            std::vector<uint16_t> tmp(len);
            for (uint32_t k = 0; k < len; ++k) tmp[k] = r.u16(q + k * 2);
            out.push_back(utf16ToUtf8(tmp.data(), tmp.size()));
        }
    }
    return out;
}

} // namespace

bool ApkResources::load(std::vector<uint8_t> apkBytes) {
    apk_ = std::move(apkBytes);
    zip_ = std::make_shared<ZipHolder>();
    if (!mz_zip_reader_init_mem(&zip_->zip, apk_.data(), apk_.size(), 0)) {
        std::cerr << "[ApkResources] not a valid zip" << std::endl;
        return false;
    }
    zip_->open = true;
    loaded_ = true;
    std::vector<uint8_t> arsc = readFile("resources.arsc");
    if (!arsc.empty()) {
        if (!parseTable(arsc)) std::cerr << "[ApkResources] resources.arsc could not be parsed" << std::endl;
        else std::cout << "[ApkResources] resource table: " << entries_.size() << " entries" << std::endl;
    }
    return true;
}

std::vector<uint8_t> ApkResources::readFile(const std::string& path) const {
    if (!zip_ || !zip_->open) return {};
    int idx = mz_zip_reader_locate_file(&zip_->zip, path.c_str(), nullptr, 0);
    if (idx < 0) return {};
    size_t sz = 0;
    void* p = mz_zip_reader_extract_to_heap(&zip_->zip, static_cast<mz_uint>(idx), &sz, 0);
    if (!p) return {};
    std::vector<uint8_t> out(static_cast<uint8_t*>(p), static_cast<uint8_t*>(p) + sz);
    mz_free(p);
    return out;
}

bool ApkResources::parseTable(const std::vector<uint8_t>& arsc) {
    Reader r{arsc.data(), arsc.size()};
    if (r.u16(0) != 0x0002) return false;
    uint16_t headerSize = r.u16(2);
    size_t pos = headerSize;
    std::vector<std::string> globalStrings;

    while (pos + 8 <= arsc.size()) {
        uint16_t type = r.u16(pos);
        uint32_t size = r.u32(pos + 4);
        if (size < 8 || pos + size > arsc.size()) break;

        if (type == 0x0001) {
            globalStrings = readStringPool(r, pos);
        } else if (type == 0x0200) {   // package
            uint32_t pkgId = r.u32(pos + 8);
            uint16_t pkgHeader = r.u16(pos + 2);
            uint32_t typeStringsOff = r.u32(pos + 8 + 4 + 256);
            uint32_t keyStringsOff = r.u32(pos + 8 + 4 + 256 + 8);
            std::vector<std::string> typeStrings = readStringPool(r, pos + typeStringsOff);
            std::vector<std::string> keyStrings = readStringPool(r, pos + keyStringsOff);
            for (size_t i = 0; i < typeStrings.size(); ++i)
                typeNames_[(pkgId << 8) | static_cast<uint32_t>(i + 1)] = typeStrings[i];

            size_t p = pos + pkgHeader;
            size_t pend = pos + size;
            while (p + 8 <= pend) {
                uint16_t ct = r.u16(p);
                uint32_t cs = r.u32(p + 4);
                if (cs < 8 || p + cs > pend) break;
                if (ct == 0x0201) {    // type chunk
                    uint8_t typeId = r.u8(p + 8);
                    uint8_t flags = r.u8(p + 9);
                    uint32_t entryCount = r.u32(p + 12);
                    uint32_t entriesStart = r.u32(p + 16);
                    uint16_t chdr = r.u16(p + 2);
                    // default configuration: every byte after the size field is zero
                    uint32_t cfgSize = r.u32(p + 20);
                    bool isDefault = true;
                    for (uint32_t k = 4; k < cfgSize && k < 64; ++k) if (r.u8(p + 20 + k) != 0) { isDefault = false; break; }

                    for (uint32_t e = 0; e < entryCount; ++e) {
                        uint32_t entryIdx = e;
                        uint32_t off;
                        if (flags & 0x01) {   // sparse
                            uint32_t v = r.u32(p + chdr + e * 4);
                            entryIdx = v & 0xFFFF;
                            off = (v >> 16) * 4;
                        } else {
                            off = r.u32(p + chdr + e * 4);
                            if (off == 0xFFFFFFFFu) continue;
                        }
                        size_t eo = p + entriesStart + off;
                        if (!r.ok(eo, 8)) continue;
                        uint16_t eflags = r.u16(eo + 2);
                        uint32_t keyIdx = r.u32(eo + 4);
                        uint32_t resId = (pkgId << 24) | (static_cast<uint32_t>(typeId) << 16) | entryIdx;
                        Entry& ent = entries_[resId];
                        if (ent.name.empty() && keyIdx < keyStrings.size()) ent.name = keyStrings[keyIdx];

                        Value v;
                        if (!(eflags & 0x0001)) {   // not a complex (bag) entry
                            uint8_t dt = r.u8(eo + 8 + 3);
                            uint32_t data = r.u32(eo + 8 + 4);
                            v.found = true; v.type = dt; v.data = data;
                            if (dt == 0x03 && data < globalStrings.size()) v.str = globalStrings[data];
                        }
                        if (isDefault) ent.values.insert(ent.values.begin(), v);
                        else ent.values.push_back(v);
                    }
                }
                p += cs;
            }
        }
        pos += size;
    }

    for (const auto& kv : entries_) {
        uint32_t id = kv.first;
        auto t = typeNames_.find(((id >> 24) << 8) | ((id >> 16) & 0xFF));
        if (t != typeNames_.end() && !kv.second.name.empty()) byName_[t->second + "/" + kv.second.name] = id;
    }
    return !entries_.empty();
}

ApkResources::Value ApkResources::resolve(uint32_t resId) const {
    for (int hop = 0; hop < 6; ++hop) {
        auto it = entries_.find(resId);
        if (it == entries_.end() || it->second.values.empty()) return {};
        const Value& v = it->second.values.front();
        if (v.found && v.type == 0x01 && v.data != 0) { resId = v.data; continue; }
        return v;
    }
    return {};
}

std::string ApkResources::typeName(uint32_t resId) const {
    auto t = typeNames_.find(((resId >> 24) << 8) | ((resId >> 16) & 0xFF));
    return t == typeNames_.end() ? std::string() : t->second;
}

std::string ApkResources::entryName(uint32_t resId) const {
    auto it = entries_.find(resId);
    return it == entries_.end() ? std::string() : it->second.name;
}

std::string ApkResources::getString(uint32_t resId) const {
    Value v = resolve(resId);
    return (v.found && v.type == 0x03) ? v.str : std::string();
}

std::string ApkResources::filePath(uint32_t resId) const {
    Value v = resolve(resId);
    return (v.found && v.type == 0x03) ? v.str : std::string();
}

uint32_t ApkResources::findId(const std::string& type, const std::string& name) const {
    auto it = byName_.find(type + "/" + name);
    return it == byName_.end() ? 0 : it->second;
}

} // namespace NativeDex::Launcher
