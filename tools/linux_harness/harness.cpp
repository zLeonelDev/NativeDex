#include <chrono>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include "bytecode_interpreter/BytecodeInterpreter.h"
#include "dex_loader/DexLoader.h"
#include "bytecode_interpreter/NativeHelpers.h"
using namespace NativeDex::Runtime;
int main(int argc, char** argv) {
    std::vector<std::string> files;
    for (auto& e : std::filesystem::directory_iterator("/tmp/dex")) files.push_back(e.path().string());
    std::sort(files.begin(), files.end());
    std::vector<std::unique_ptr<DexLoader::DexLoader>> loaders;
    std::vector<const DexLoader::DexFile*> dex;
    auto t0 = std::chrono::steady_clock::now();
    for (auto& f : files) {
        auto l = std::make_unique<DexLoader::DexLoader>();
        if (!l->loadFromFile(f)) { std::cerr << "fail " << f << "\n"; continue; }
        dex.push_back(&l->getDexFile());
        loaders.push_back(std::move(l));
    }
    auto t1 = std::chrono::steady_clock::now();
    std::cerr << "loaded " << dex.size() << " dex in " << std::chrono::duration<double>(t1 - t0).count() << "s\n";
    Heap::HeapManager heap(1 << 20);
    bool debug = argc > 3 && std::string(argv[3]) == "debug";
    BytecodeInterpreter::BytecodeInterpreter interp(dex, heap, debug);
    if (argc < 2) {
        std::cerr << "usage: harness <Activity descriptor> [<Application descriptor>|-] [debug]\n";
        return 2;
    }
    std::string cls = argv[1];
    std::string appCls = argc > 2 ? argv[2] : "-";
    std::cerr << "natives registered: " << BytecodeInterpreter::NativeRegistry::instance().count() << "\n";
    auto t2 = std::chrono::steady_clock::now();
    interp.vm().props["package"] = std::getenv("ND_PACKAGE") ? std::getenv("ND_PACKAGE") : "app.under.test";
    interp.vm().runGuarded([&] { BytecodeInterpreter::frameworkMarkMainThread(interp.vm()); }, "main");
    BytecodeInterpreter::Object* app = appCls == "-" ? nullptr : interp.newObject(appCls);
    if (appCls != "-") std::cerr << "application -> " << (app ? "ok" : "null") << "\n";
    if (app) {
        BytecodeInterpreter::frameworkSetAppContext(app);
        BytecodeInterpreter::Value ctx; ctx.ref = app;
        interp.callMethod(app, "attachBaseContext", "(Landroid/content/Context;)V", {ctx});
        bool ra = interp.callMethod(app, "onCreate", "()V");
        std::cerr << "app.onCreate -> " << ra << "\n";
        interp.vm().pumpQueue();
    }
    auto* o = interp.newObject(cls);
    std::cerr << "newObject -> " << (o ? "ok" : "null") << "\n";
    if (o) {
        bool r = interp.callMethod(o, "onCreate", "(Landroid/os/Bundle;)V", {BytecodeInterpreter::Value()});
        std::cerr << "onCreate -> " << r << "\n";
    }
    auto t3 = std::chrono::steady_clock::now();
    std::cerr << "ran in " << std::chrono::duration<double>(t3 - t2).count() << "s\n";
}
