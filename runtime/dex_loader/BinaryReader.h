#pragma once

#include <vector>
#include <cstdint>
#include <string_view>
#include <stdexcept>
#include <cstring>

namespace NativeDex::Runtime::DexLoader {

class BinaryReader {
public:
    explicit BinaryReader(const std::vector<uint8_t>& data) : data_(data), pos_(0) {}
    explicit BinaryReader(std::vector<uint8_t>&& data) : data_(std::move(data)), pos_(0) {}
    explicit BinaryReader(const uint8_t* data, size_t size) : data_(data, data + size), pos_(0) {}
    explicit BinaryReader(const std::string& data) : data_(data.begin(), data.end()), pos_(0) {}

    void readBytes(uint8_t* dest, size_t size) {
        checkBounds(size);
        std::memcpy(dest, &data_[pos_], size);
        pos_ += size;
    }

    uint8_t readU8() {
        checkBounds(1);
        return data_[pos_++];
    }

    uint16_t readU16() {
        checkBounds(2);
        uint16_t val = static_cast<uint16_t>(data_[pos_]) |
                      (static_cast<uint16_t>(data_[pos_ + 1]) << 8);
        pos_ += 2;
        return val;
    }

    uint32_t readU32() {
        checkBounds(4);
        uint32_t val = static_cast<uint32_t>(data_[pos_]) |
                      (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
                      (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
                      (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return val;
    }

    uint64_t readU64() {
        checkBounds(8);
        uint64_t val = static_cast<uint64_t>(readU32()) |
                      (static_cast<uint64_t>(readU32()) << 32);
        return val;
    }

    uint32_t readUleb128() {
        uint32_t result = 0;
        int shift = 0;
        while (true) {
            checkBounds(1);
            uint8_t byte = data_[pos_++];
            result |= static_cast<uint32_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) break;
            shift += 7;
            if (shift >= 32) throw std::runtime_error("ULEB128 overflow");
        }
        return result;
    }

    int32_t readSleb128() {
        int32_t result = 0;
        int shift = 0;
        uint8_t byte;
        while (true) {
            checkBounds(1);
            byte = data_[pos_++];
            result |= static_cast<int32_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) break;
            shift += 7;
            if (shift >= 32) throw std::runtime_error("SLEB128 overflow");
        }
        if ((byte & 0x40) && shift < 32) {
            result |= -1 << shift;
        }
        return result;
    }

    std::string_view readString(size_t length) {
        checkBounds(length);
        std::string_view result(reinterpret_cast<const char*>(data_.data() + pos_), length);
        pos_ += length;
        return result;
    }

    std::string_view readZeroTerminatedString() {
        size_t start = pos_;
        while (pos_ < data_.size() && data_[pos_] != 0) {
            ++pos_;
        }
        if (pos_ > data_.size()) throw std::runtime_error("Unterminated string");
        std::string_view result(reinterpret_cast<const char*>(data_.data() + start), pos_ - start);
        if (pos_ < data_.size() && data_[pos_] == 0) ++pos_; // skip null
        return result;
    }

    void seek(size_t offset) {
        if (offset > data_.size()) throw std::out_of_range("Seek past end of data");
        pos_ = offset;
    }

    size_t tell() const noexcept { return pos_; }
    size_t size() const noexcept { return data_.size(); }
    bool eof() const noexcept { return pos_ >= data_.size(); }
    size_t remaining() const noexcept { return data_.size() - pos_; }
    const uint8_t* data() const noexcept { return data_.data(); }
    const std::vector<uint8_t>& vector() const noexcept { return data_; }

private:
    void checkBounds(size_t needed) const {
        if (pos_ + needed > data_.size()) {
            throw std::out_of_range("BinaryReader: read past end of data");
        }
    }

    std::vector<uint8_t> data_;
    size_t pos_ = 0;
};

} // namespace NativeDex::Runtime::DexLoader