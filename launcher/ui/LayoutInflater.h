#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include "../BinaryAndroidXmlParser.h"
#include "NativeViewRegistry.h"

namespace NativeDex::Launcher {

// Inflates binary Android XML layouts from the APK into the native view
// tree. Layout resources are resolved through the APK's resource table
// (resources.arsc), so shrunk/obfuscated resource paths work as well as
// plain ones. Nothing here is specific to any application.
class LayoutInflater {
public:
    static LayoutInflater& instance();

    // Inflates the layout resource with id `resId` (R.layout.*). Returns the
    // root view id, or 0 when the resource is missing or not a layout.
    uint64_t inflateById(uint32_t resId);

    // Inflates a layout file inside the APK ("res/layout/foo.xml").
    uint64_t inflateFile(const std::string& path);

    // Java class descriptor of the view with registry id `viewId`
    // ("Landroid/widget/TextView;" or an app-defined custom view class).
    std::string descriptorOf(uint64_t viewId) const;

    // Remember the descriptor of a view created outside of inflation.
    void setDescriptor(uint64_t viewId, const std::string& descriptor);

    void reset();

private:
    LayoutInflater() = default;
    uint64_t inflateNode(const XmlNode& node, int depth);

    std::unordered_map<uint64_t, std::string> descriptors_;
};

// dp -> px used for all layout dimensions.
constexpr float kDensity = 2.0f;

} // namespace NativeDex::Launcher
