#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include "../../runtime/bytecode_interpreter/BytecodeInterpreter.h"
#include "../../platform/windows/window/Win32Window.h"
#include "../../platform/windows/graphics/Direct2DGraphics.h"
#include "../../platform/windows/input/Win32Input.h"
#include "../../android/android.os/Handler.h"
#include "../../android/android.os/Looper.h"

namespace NativeDex::Launcher {

class Activity;

class ActivityManager {
public:
    ActivityManager(NativeDex::Platform::Windows::Win32Window& window, 
                    NativeDex::Platform::Windows::Direct2DGraphics& graphics, 
                    NativeDex::Platform::Windows::Win32Input& input);
    ~ActivityManager();
    
    void initialize(NativeDex::Runtime::BytecodeInterpreter::BytecodeInterpreter* interpreter, 
                    std::vector<const NativeDex::Runtime::DexLoader::DexFile*> dexFiles);
    bool launchActivity(const std::string& activityDescriptor);
    void finishActivity(Activity* activity);
    
    Activity* getCurrentActivity() const { return currentActivity_; }

    // Global access (single-activity launcher). Used by the native view
    // registry to measure/layout against the actual window size when
    // setContentView is called from the interpreter.
    static ActivityManager* global() { return s_global; }
    void getWindowSize(int& w, int& h) const;
    
    // Called from UI thread
    void onPause();
    void onResume();
    void onConfigurationChanged();
    void onLowMemory();
    
    // Render current activity
    void render();

    // Ask the OS window to repaint (used by the native bridge when a
    // setContentView call changes the view tree).
    void requestInvalidate();
    
    // Handle input events
    void onKeyEvent(const NativeDex::Platform::Windows::Win32Input::AndroidKeyEvent& event);
    void onTouchEvent(const NativeDex::Platform::Windows::Win32Input::AndroidMotionEvent& event);

private:
    NativeDex::Platform::Windows::Win32Window& window_;
    NativeDex::Platform::Windows::Direct2DGraphics& graphics_;
    NativeDex::Platform::Windows::Win32Input& input_;
    
    NativeDex::Runtime::BytecodeInterpreter::BytecodeInterpreter* interpreter_ = nullptr;
    std::vector<const NativeDex::Runtime::DexLoader::DexFile*> dexFiles_;
    
    Activity* currentActivity_ = nullptr;
    std::vector<std::unique_ptr<Activity>> activityStack_;
    
    // UI thread handler
    std::unique_ptr<NativeDex::Android::OS::Handler> uiHandler_;
    
    // Returns the DEX file and type index for the given descriptor.
    // descriptor: e.g. "Lcom/example/MyActivity;"
    const NativeDex::Runtime::DexLoader::DexFile* findTypeIndex(
        const std::string& descriptor, uint32_t& outTypeIdx) const;
    int findMethodIndex(const NativeDex::Runtime::DexLoader::DexFile* dex,
                        uint32_t classIdx, const std::string& methodName,
                        const NativeDex::Runtime::DexLoader::DexFile*& outDex) const;
    bool invokeLifecycleMethod(Activity* activity, const std::string& methodName);

    static ActivityManager* s_global;
};

} // namespace NativeDex::Launcher
