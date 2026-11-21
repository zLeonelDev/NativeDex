#pragma once

#include <string>
#include "BinaryAndroidXmlParser.h"

namespace NativeDex::Launcher {

bool createNativeWindow(const std::string& title, const XmlNode* layoutRoot = nullptr);
int runNativeMessageLoop();

} // namespace NativeDex::Launcher
