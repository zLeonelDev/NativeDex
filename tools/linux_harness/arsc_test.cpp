#include "core/ApkResources.h"
#include <fstream>
#include <iostream>
using namespace NativeDex::Launcher;
int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: arsc_test <app.apk>\n"; return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    auto& r = ApkResources::instance();
    std::cout << "load " << r.load(std::move(b)) << "\n";
    // scan some ids of type layout (type id typically small) 
    int shown = 0;
    for (uint32_t t = 1; t < 40 && shown < 6; ++t)
        for (uint32_t e = 0; e < 4000 && shown < 6; ++e) {
            uint32_t id = 0x7f000000 | (t << 16) | e;
            if (r.typeName(id) == "layout" && !r.entryName(id).empty()) {
                std::cout << std::hex << id << " " << r.entryName(id) << " -> " << r.filePath(id) << "\n"; ++shown;
            }
        }
    uint32_t id = r.findId("string", "app_name");
    std::cout << "app_name id=" << std::hex << id << " = '" << r.getString(id) << "'\n";
}
