#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <variant>
#include <cstdint>

namespace NativeDex::Android::OS {

class Bundle {
public:
    Bundle() = default;
    ~Bundle() = default;
    
    // Copy/Move
    Bundle(const Bundle& other);
    Bundle& operator=(const Bundle& other);
    Bundle(Bundle&& other) noexcept;
    Bundle& operator=(Bundle&& other) noexcept;
    
    // Put values
    void putString(const std::string& key, const std::string& value);
    void putInt(const std::string& key, int32_t value);
    void putLong(const std::string& key, int64_t value);
    void putFloat(const std::string& key, float value);
    void putDouble(const std::string& key, double value);
    void putBoolean(const std::string& key, bool value);
    void putStringArray(const std::string& key, const std::vector<std::string>& value);
    void putIntArray(const std::string& key, const std::vector<int32_t>& value);
    void putLongArray(const std::string& key, const std::vector<int64_t>& value);
    void putFloatArray(const std::string& key, const std::vector<float>& value);
    void putDoubleArray(const std::string& key, const std::vector<double>& value);
    void putBooleanArray(const std::string& key, const std::vector<bool>& value);
    void putBundle(const std::string& key, const Bundle& value);
    void putParcelable(const std::string& key, const std::shared_ptr<void>& value); // Simplified
    
    // Get values
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    int32_t getInt(const std::string& key, int32_t defaultValue = 0) const;
    int64_t getLong(const std::string& key, int64_t defaultValue = 0) const;
    float getFloat(const std::string& key, float defaultValue = 0.0f) const;
    double getDouble(const std::string& key, double defaultValue = 0.0) const;
    bool getBoolean(const std::string& key, bool defaultValue = false) const;
    std::vector<std::string> getStringArray(const std::string& key) const;
    std::vector<int32_t> getIntArray(const std::string& key) const;
    std::vector<int64_t> getLongArray(const std::string& key) const;
    std::vector<float> getFloatArray(const std::string& key) const;
    std::vector<double> getDoubleArray(const std::string& key) const;
    std::vector<bool> getBooleanArray(const std::string& key) const;
    Bundle getBundle(const std::string& key) const;
    
    // Check
    bool containsKey(const std::string& key) const;
    bool isEmpty() const { return data_.empty(); }
    size_t size() const { return data_.size(); }
    
    // Remove
    void remove(const std::string& key);
    void clear();
    
    // Keys
    std::vector<std::string> keySet() const;
    
    // Parcel (simplified)
    std::vector<uint8_t> toByteArray() const;
    static Bundle fromByteArray(const std::vector<uint8_t>& data);

private:
    using Value = std::variant<
        std::monostate,
        std::string,
        int32_t,
        int64_t,
        float,
        double,
        bool,
        std::vector<std::string>,
        std::vector<int32_t>,
        std::vector<int64_t>,
        std::vector<float>,
        std::vector<double>,
        std::vector<bool>,
        Bundle
    >;
    
    std::unordered_map<std::string, Value> data_;
};

} // namespace NativeDex::Android::OS