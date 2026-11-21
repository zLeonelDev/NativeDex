#include <windows.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <memory>
#include <future>

#include "../../runtime/dex_loader/DexLoader.h"
#include "../../runtime/bytecode_interpreter/BytecodeInterpreter.h"
#include "../../runtime/heap/HeapManager.h"
#include "../../runtime/class_loader/ClassLoader.h"
#include "../../platform/windows/window/Win32Window.h"
#include "../../platform/windows/graphics/Direct2DGraphics.h"
#include "../../platform/windows/input/Win32Input.h"
#include "../../platform/windows/threading/Win32Thread.h"
#include "../../platform/windows/filesystem/Win32FileSystem.h"
#include "../../platform/windows/time/Win32Time.h"
#include "../../android/android.os/Looper.h"
#include "../../android/android.os/Handler.h"
#include "../../launcher/ui/WindowManager.h"
#include "../../launcher/ui/ActivityManager.h"
#include "../../launcher/core/Bootstrap.h"
#include "../../launcher/ui/NativeBridge.h"
#include "../../launcher/ui/NativeViewRegistry.h"
#include "../../launcher/ui/LayoutInflater.h"
#include "../../third_party/miniz/miniz.h"

namespace fs = std::filesystem;

using namespace NativeDex::Runtime;
using namespace NativeDex::Platform::Windows;
using namespace NativeDex::Android::OS;
using namespace NativeDex::Launcher;

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    FILE* f = nullptr;
    if (fopen_s(&f, "crash.log", "w") == 0) {
        fprintf(f, "Code: 0x%08X\nAddr: %p\n", ep->ExceptionRecord->ExceptionCode,
                ep->ExceptionRecord->ExceptionAddress);

        HMODULE hMod = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(ep->ExceptionRecord->ExceptionAddress), &hMod)) {
            wchar_t modName[MAX_PATH] = {};
            GetModuleFileNameW(hMod, modName, MAX_PATH);
            fprintf(f, "Module: %ws\n", modName);
        }

        void* stack[32] = {};
        USHORT frames = RtlCaptureStackBackTrace(0, 32, stack, nullptr);
        fprintf(f, "Stack (%u frames):\n", frames);
        for (USHORT i = 0; i < frames; ++i) {
            HMODULE hMod = nullptr;
            uintptr_t modBase = 0;
            char modName[256] = "?";
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(stack[i]), &hMod)) {
                modBase = reinterpret_cast<uintptr_t>(hMod);
                wchar_t wname[MAX_PATH] = {};
                if (GetModuleFileNameA(hMod, modName, sizeof(modName))) {
                    const char* slash = strrchr(modName, '\\');
                    if (slash) {
                        std::string name = slash + 1;
                        strncpy_s(modName, name.c_str(), sizeof(modName) - 1);
                    }
                }
            }
            fprintf(f, "  %p  %s+0x%llX\n", stack[i], modName,
                    static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(stack[i]) - modBase));
        }
        fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    SetUnhandledExceptionFilter(CrashHandler);

    // Parse command line
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    
    bool debug = false;
    bool scanOnly = false;
    std::string apkPath;
    
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--debug") {
            debug = true;
        } else if (arg == L"--scan") {
            scanOnly = true;
        } else if (apkPath.empty()) {
            // Convert to UTF-8 (length-based so we don't include the null terminator)
            int size = WideCharToMultiByte(CP_UTF8, 0, arg.c_str(), static_cast<int>(arg.size()), nullptr, 0, nullptr, nullptr);
            std::string utf8(size, 0);
            WideCharToMultiByte(CP_UTF8, 0, arg.c_str(), static_cast<int>(arg.size()), &utf8[0], size, nullptr, nullptr);
            apkPath = utf8;
        }
    }
    
    LocalFree(argv);
    
    if (debug) {
        std::cout << "[NativeDex] Starting runtime..." << std::endl;
    }
    
    if (apkPath.empty()) {
        std::cerr << "[NativeDex] Error: Missing APK path argument" << std::endl;
        std::cerr << "Usage: NativeDex [--debug] [--scan] <apk_path>" << std::endl;
        return 1;
    }
    
    if (!fs::exists(apkPath)) {
        std::cerr << "[NativeDex] Error: APK file not found: " << apkPath << std::endl;
        return 1;
    }
    
    // Initialize COM for Direct2D
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        std::cerr << "[NativeDex] Failed to initialize COM" << std::endl;
        return 1;
    }
    
    // Create main window
    Win32Window window;
    Win32Window::Config windowConfig;
    windowConfig.title = L"NativeDex";
    windowConfig.width = 800;
    windowConfig.height = 600;
    windowConfig.resizable = true;
    
    if (!window.create(windowConfig)) {
        std::cerr << "[NativeDex] Failed to create window" << std::endl;
        CoUninitialize();
        return 1;
    }
    
    // Initialize graphics
    Direct2DGraphics graphics;
    Direct2DGraphics::Config graphicsConfig;
    graphicsConfig.hwnd = window.getHandle();
    graphicsConfig.width = 800;
    graphicsConfig.height = 600;
    
    if (!graphics.initialize(graphicsConfig)) {
        std::cerr << "[NativeDex] Failed to initialize graphics" << std::endl;
        window.destroy();
        graphics.shutdown();
        CoUninitialize();
        return 1;
    }
    
    // Initialize input
    Win32Input input;
    input.initialize(window.getHandle());
    
    // Set up window callbacks
    window.setPaintCallback([&graphics](HDC hdc) {
        graphics.beginDraw();
        graphics.clear(Direct2DGraphics::Color(0.1f, 0.1f, 0.12f, 1.0f));
        
        // Draw loading text
        Direct2DGraphics::Rect textRect(50, 50, 700, 150);
        graphics.drawText(L"Loading APK...", textRect, Direct2DGraphics::Color(1, 1, 1, 1), 24.0f, L"Segoe UI");
        
        graphics.endDraw();
    });
    
    window.setResizeCallback([&graphics, &window](int width, int height) {
        graphics.resize(width, height);
    });
    
    window.show(nCmdShow);
    
    // Prepare APK loading
    Bootstrap::Options bootstrapOptions;
    bootstrapOptions.debug = debug;
    bootstrapOptions.scanOnly = scanOnly;
    bootstrapOptions.apkPath = apkPath;
    
    Bootstrap bootstrap(bootstrapOptions);
    
    // Load APK asynchronously
    std::promise<BootstrapResult> promise;
    auto future = promise.get_future();
    
    bootstrap.startAsync([&promise, &window, &graphics, debug](BootstrapResult result) {
        promise.set_value(std::move(result));
    });
    
    // Run message loop while loading
    MSG msg = {};
    bool loading = true;
    
    while (loading) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                loading = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        if (future.wait_for(std::chrono::milliseconds(16)) == std::future_status::ready) {
            loading = false;
        }
        
        // Keep window responsive
        window.getHandle();
    }
    
    BootstrapResult result = future.get();
    
    if (!result.success) {
        std::cerr << "[NativeDex] Bootstrap failed: " << result.errorMessage << std::endl;
        
        // Build the error message text
        std::wstring errorMsg = L"Failed to load APK: ";
        int size = MultiByteToWideChar(CP_UTF8, 0, result.errorMessage.c_str(), -1, nullptr, 0);
        std::wstring wideError(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, result.errorMessage.c_str(), -1, &wideError[0], size);
        errorMsg += wideError;
        
        // Show the error via the paint callback so it stays on screen
        window.setPaintCallback([&graphics, errorMsg](HDC) {
            graphics.beginDraw();
            graphics.clear(Direct2DGraphics::Color(0.1f, 0.1f, 0.12f, 1.0f));
            Direct2DGraphics::Rect textRect(50, 50, 700, 150);
            graphics.drawText(errorMsg, textRect, Direct2DGraphics::Color(1, 0.3f, 0.3f, 1), 18.0f, L"Segoe UI");
            graphics.endDraw();
        });
        window.invalidate();
        
        // Wait for user to close
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        window.destroy();
        graphics.shutdown();
        CoUninitialize();
        return 1;
    }
    
    if (debug) {
        std::cout << "[NativeDex] APK loaded successfully" << std::endl;
        std::cout << "[NativeDex] Package: " << result.packageName << std::endl;
        std::cout << "[NativeDex] Launcher Activity: " << result.launcherActivity << std::endl;
    }
    
    if (scanOnly) {
        window.destroy();
        graphics.shutdown();
        CoUninitialize();
        return 0;
    }
    
    // Set up window with app info
    std::wstring appTitle = L"NativeDex - ";
    int size = MultiByteToWideChar(CP_UTF8, 0, result.appLabel.c_str(), -1, nullptr, 0);
    std::wstring wideLabel(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, result.appLabel.c_str(), -1, &wideLabel[0], size);
    appTitle += wideLabel;
    window.setTitle(appTitle);
    std::cout << "[NativeDex] Stage: title set" << std::endl;

    // Find the DEX file that contains the launcher activity.
    // The APK's ZIP layout is not dex-number ordered (classes2.dex came
    // first), so the primary dex must be the one that defines the activity.
    std::string launcherDescriptor;
    if (!result.launcherActivity.empty()) {
        launcherDescriptor = "L";
        for (char c : result.launcherActivity) {
            launcherDescriptor += (c == '.') ? '/' : c;
        }
        launcherDescriptor += ";";
    }

    // Collect pointers to ALL DEX files for multi-DEX support.
    std::vector<const DexLoader::DexFile*> allDexFiles;
    allDexFiles.reserve(result.dexFiles.size());
    for (auto& dex : result.dexFiles) {
        allDexFiles.push_back(&dex);
    }
    std::cout << "[NativeDex] Loaded " << allDexFiles.size()
              << " DEX files" << std::endl;

    // Initialize runtime — pass ALL DEX files to the interpreter.
    Heap::HeapManager heapManager(64 * 1024 * 1024); // 64MB
    std::cout << "[NativeDex] Stage: heap created" << std::endl;
    BytecodeInterpreter::BytecodeInterpreter interpreter(
        allDexFiles, heapManager, debug);
    std::cout << "[NativeDex] Stage: interpreter created (multidex)" << std::endl;
    
    // Initialize Android framework
    Looper::prepareMainLooper();
    std::cout << "[NativeDex] Stage: looper prepared" << std::endl;

    // Install the native method bridge. This must happen *before* the
    // interpreter starts running bytecode so the Android framework calls
    // resolve to C++ implementations.
    NativeBridge::instance().installDefaults();

    // Extract assets/res from the APK so LayoutInflater can read res/layout/*.xml
    // and assets/www/index.html without re-parsing the ZIP each time.
    {
        std::string apkRoot = "tmp";
        std::error_code ec;
        std::filesystem::create_directories(apkRoot + "\\res\\layout", ec);
        std::filesystem::create_directories(apkRoot + "\\assets\\www", ec);

        auto apkBytes = NativeDex::Platform::Windows::Win32FileSystem::readAllBytes(apkPath);
        mz_zip_archive zip;
        memset(&zip, 0, sizeof(zip));
        if (mz_zip_reader_init_mem(&zip, apkBytes.data(), apkBytes.size(), 0)) {
            int n = mz_zip_reader_get_num_files(&zip);
            for (int i = 0; i < n; ++i) {
                mz_zip_archive_file_stat fs;
                if (!mz_zip_reader_file_stat(&zip, i, &fs)) continue;
                std::string name = fs.m_filename;
                bool want = (name.rfind("res/", 0) == 0 && name.find("/layout/") != std::string::npos)
                         || name.rfind("assets/", 0) == 0;
                if (!want) continue;
                std::string out = apkRoot + "\\" + name;
                std::replace(out.begin(), out.end(), '/', '\\');
                std::error_code ec2;
                std::filesystem::create_directories(std::filesystem::path(out).parent_path(), ec2);
                if (!mz_zip_reader_extract_to_file(&zip, i, out.c_str(), 0)) {
                    std::cerr << "[NativeDex] Failed to extract " << name << std::endl;
                }
            }
            mz_zip_reader_end(&zip);
        }
        LayoutInflater::instance().setApkRoot(apkRoot);
        std::cout << "[NativeDex] APK extracted to " << apkRoot << "\\" << std::endl;
    }
    
    // Create activity manager
    ActivityManager activityManager(window, graphics, input);
    std::cout << "[NativeDex] Stage: activity manager created" << std::endl;
    activityManager.initialize(&interpreter, allDexFiles);
    std::cout << "[NativeDex] Stage: activity manager initialized" << std::endl;
    
    // Launch main activity
    if (!launcherDescriptor.empty()) {
        std::cout << "[NativeDex] Stage: launching " << launcherDescriptor << std::endl;
        if (!activityManager.launchActivity(launcherDescriptor)) {
            std::cerr << "[NativeDex] Failed to launch activity: " << result.launcherActivity << std::endl;
        }
        std::cout << "[NativeDex] Stage: launch done" << std::endl;

        // Force a repaint now that the activity has had a chance to inflate
        // its content view through the native bridge.
        window.invalidate();
    } else {
        // No activity launched, update screen to remove "Loading APK..."
        window.setPaintCallback([&graphics](HDC hdc) {
            graphics.beginDraw();
            graphics.clear(Direct2DGraphics::Color(0.1f, 0.1f, 0.12f, 1.0f));
            
            Direct2DGraphics::Rect textRect(50, 50, 700, 150);
            std::wstring msg = L"APK Loaded.\nNo main activity found in Manifest.";
            graphics.drawText(msg, textRect, Direct2DGraphics::Color(0.5f, 1.0f, 0.5f, 1.0f), 20.0f, L"Segoe UI");
            
            graphics.endDraw();
        });
        window.invalidate();
    }
    
    // Main message loop. We run a background thread that pumps the Android
    // Looper (so Handler.post(Runnable) actually executes), while the main
    // thread keeps handling Win32 messages.
    std::thread looperThread([]() {
        try {
            Looper::loop();
        } catch (...) {
            // Swallow exceptions from the looper thread so the launcher
            // does not abort on handler errors.
        }
    });

    int exitCode = window.runMessageLoop();

    // Cleanup
    Looper::getMainLooper()->quit();
    if (looperThread.joinable()) looperThread.join();
    window.destroy();
    graphics.shutdown();
    CoUninitialize();

    return exitCode;
}