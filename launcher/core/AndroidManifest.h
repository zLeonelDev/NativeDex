#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace NativeDex::Launcher {

struct ManifestActivity {
    std::string name;       // Dotted class name (resolved)
    bool isLauncher = false;
};

struct AndroidManifest {
    std::string packageName;
    std::string appLabel;
    std::string launcherActivity;   // Dotted class name of the MAIN/LAUNCHER activity
    std::string applicationClass;   // Dotted class name from <application android:name>, if any
    std::vector<ManifestActivity> activities;
};

// Parses Android's binary XML (AXML) format, specifically the subset needed to
// extract the launcher activity, package name and label from AndroidManifest.xml.
class BinaryXmlParser {
public:
    explicit BinaryXmlParser(const std::vector<uint8_t>& data);

    // Returns true if the data was a well-formed binary XML document.
    // packageName / appLabel / launcherActivity are populated when found.
    bool parse(AndroidManifest& out);

private:
    const std::vector<uint8_t>& data_;

    std::vector<std::string> strings_;
    std::vector<std::string> elementStack_;
    std::vector<ManifestActivity> activityStack_;
    std::string filterAction_;
    std::string filterCategory_;

    bool readStringPool(size_t base, size_t chunkSize);
    void parseStartElement(size_t base, AndroidManifest& out);
    void parseEndElement(size_t base, AndroidManifest& out);

    uint8_t readU8(size_t off) const;
    uint16_t readU16(size_t off) const;
    uint32_t readU32(size_t off) const;
    const std::string* getString(uint32_t idx) const;

    static std::string resolveActivityName(const std::string& name, const std::string& package);
};

} // namespace NativeDex::Launcher
