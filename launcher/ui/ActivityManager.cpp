#include "ActivityManager.h"
#include "Activity.h"
#include "NativeViewRegistry.h"
#include "LayoutInflater.h"
#include <iostream>

namespace NativeDex::Launcher {

ActivityManager* ActivityManager::s_global = nullptr;

ActivityManager::ActivityManager(NativeDex::Platform::Windows::Win32Window& window, 
                                NativeDex::Platform::Windows::Direct2DGraphics& graphics, 
                                NativeDex::Platform::Windows::Win32Input& input)
    : window_(window), graphics_(graphics), input_(input) {
    s_global = this;
}

ActivityManager::~ActivityManager() {
    while (!activityStack_.empty()) {
        finishActivity(activityStack_.back().get());
    }
    if (s_global == this) s_global = nullptr;
}

void ActivityManager::getWindowSize(int& w, int& h) const {
    window_.getClientSize(w, h);
    if (w <= 0) w = 800;
    if (h <= 0) h = 600;
}

void ActivityManager::initialize(
    NativeDex::Runtime::BytecodeInterpreter::BytecodeInterpreter* interpreter, 
    std::vector<const NativeDex::Runtime::DexLoader::DexFile*> dexFiles) {
    interpreter_ = interpreter;
    dexFiles_ = std::move(dexFiles);
    
    uiHandler_ = std::make_unique<NativeDex::Android::OS::Handler>(
        NativeDex::Android::OS::Looper::getMainLooper());
}

bool ActivityManager::launchActivity(const std::string& activityDescriptor) {
    if (!interpreter_ || dexFiles_.empty()) {
        std::cerr << "[ActivityManager] Not initialized" << std::endl;
        return false;
    }
    
    // Find the DEX that contains this class and its type index.
    uint32_t typeIdx = UINT32_MAX;
    const auto* activityDex = findTypeIndex(activityDescriptor, typeIdx);
    if (!activityDex) {
        std::cerr << "[ActivityManager] Activity class not found: "
                  << activityDescriptor << std::endl;
        return false;
    }
    
    auto activity = std::make_unique<Activity>(*this, typeIdx, activityDescriptor);
    
    if (!invokeLifecycleMethod(activity.get(), "onCreate")) {
        std::cerr << "[ActivityManager] Failed to call onCreate" << std::endl;
        return false;
    }
    
    if (currentActivity_) {
        invokeLifecycleMethod(currentActivity_, "onPause");
    }
    
    activityStack_.push_back(std::move(activity));
    currentActivity_ = activityStack_.back().get();

    if (!NativeViewRegistry::instance().currentContentView()) {
        std::cerr << "[ActivityManager] activity set no content view; showing an empty window" << std::endl;
    }

    invokeLifecycleMethod(currentActivity_, "onStart");
    invokeLifecycleMethod(currentActivity_, "onResume");
    
    window_.setPaintCallback([this](HDC) {
        render();
    });
    window_.invalidate();
    
    input_.setKeyCallback(
        [this](const NativeDex::Platform::Windows::Win32Input::KeyEvent& event) {
            auto androidEvent = input_.convertKeyEvent(event);
            onKeyEvent(androidEvent);
        });
    input_.setTouchCallback(
        [this](const NativeDex::Platform::Windows::Win32Input::TouchEvent& event) {
            auto androidEvent = input_.convertTouchEvent(event);
            onTouchEvent(androidEvent);
        });
    
    return true;
}

void ActivityManager::finishActivity(Activity* activity) {
    if (!activity) return;
    
    invokeLifecycleMethod(activity, "onPause");
    invokeLifecycleMethod(activity, "onStop");
    invokeLifecycleMethod(activity, "onDestroy");
    
    for (auto it = activityStack_.begin(); it != activityStack_.end(); ++it) {
        if (it->get() == activity) {
            activityStack_.erase(it);
            break;
        }
    }
    
    if (currentActivity_ == activity) {
        currentActivity_ = activityStack_.empty() ? nullptr
                                                  : activityStack_.back().get();
    }
}

void ActivityManager::onPause() {
    if (currentActivity_) invokeLifecycleMethod(currentActivity_, "onPause");
}

void ActivityManager::onResume() {
    if (currentActivity_) invokeLifecycleMethod(currentActivity_, "onResume");
}

void ActivityManager::onConfigurationChanged() {
    if (currentActivity_)
        invokeLifecycleMethod(currentActivity_, "onConfigurationChanged");
}

void ActivityManager::onLowMemory() {
    if (currentActivity_) invokeLifecycleMethod(currentActivity_, "onLowMemory");
}

void ActivityManager::render() {
    graphics_.beginDraw();
    graphics_.clear(NativeDex::Platform::Windows::Direct2DGraphics::Color(
        0.95f, 0.95f, 0.97f, 1.0f));

    if (auto* v = NativeViewRegistry::instance().currentContentView()) {
        int w = 800, h = 600;
        getWindowSize(w, h);
        v->measure((w << 2) | 0, (h << 2) | 0);
        v->layout(0, 0, w, h);
        v->draw(graphics_);
    } else if (currentActivity_) {
        currentActivity_->draw(graphics_);
    }

    graphics_.endDraw();
}

void ActivityManager::onKeyEvent(
    const NativeDex::Platform::Windows::Win32Input::AndroidKeyEvent& event) {
    if (currentActivity_) currentActivity_->onKeyEvent(event);
}

void ActivityManager::onTouchEvent(
    const NativeDex::Platform::Windows::Win32Input::AndroidMotionEvent& event) {
    if (currentActivity_) currentActivity_->onTouchEvent(event);
}

void ActivityManager::requestInvalidate() {
    window_.invalidate();
}

// Search all DEX files for the given class descriptor.
// Returns the DEX file that contains it and writes the type index to outTypeIdx.
const NativeDex::Runtime::DexLoader::DexFile* ActivityManager::findTypeIndex(
    const std::string& descriptor, uint32_t& outTypeIdx) const {
    for (auto* dex : dexFiles_) {
        if (!dex) continue;
        for (uint32_t i = 0; i < dex->types.size(); ++i) {
            if (dex->types.getTypeDescriptor(i, dex->strings) == descriptor) {
                // Verify the class is actually defined (has a classDef) in this DEX.
                if (dex->findClass(descriptor) >= 0) {
                    outTypeIdx = i;
                    return dex;
                }
            }
        }
    }
    // Fallback: accept a type reference even without a classDef (the class may
    // be defined in another DEX but referenced here).
    for (auto* dex : dexFiles_) {
        if (!dex) continue;
        for (uint32_t i = 0; i < dex->types.size(); ++i) {
            if (dex->types.getTypeDescriptor(i, dex->strings) == descriptor) {
                outTypeIdx = i;
                return dex;
            }
        }
    }
    outTypeIdx = UINT32_MAX;
    return nullptr;
}

// Search all DEX files for a method with the given classIdx and name.
int ActivityManager::findMethodIndex(
    const NativeDex::Runtime::DexLoader::DexFile* dex,
    uint32_t classIdx, const std::string& methodName,
    const NativeDex::Runtime::DexLoader::DexFile*& outDex) const {
    // classIdx is DEX-local. Only use it within the correct DEX.
    if (dex) {
        // 1) Direct match in the activity's DEX.
        for (uint32_t i = 0; i < dex->methods.size(); ++i) {
            const auto& method = dex->methods.getMethod(i);
            if (method.classIdx == classIdx) {
                std::string name(dex->strings.getString(method.nameIdx));
                if (name == methodName) { outDex = dex; return static_cast<int>(i); }
            }
        }
    }

    // 2) Walk superclass chain across all DEX files to find inherited methods.
    //    classIdx is a TYPE index. Use getSuperClassByTypeIdx (which resolves
    //    through class defs) to walk the hierarchy.
    std::vector<std::string> hierarchy;
    if (dex) {
        hierarchy.push_back(std::string(dex->getTypeDescriptor(classIdx)));
        auto superDesc = std::string(dex->getSuperClassByTypeIdx(classIdx));
        for (uint32_t depth = 0; depth < 20 && !superDesc.empty(); ++depth) {
            hierarchy.push_back(superDesc);
            bool found = false;
            for (auto* d : dexFiles_) {
                if (!d) continue;
                int superTypeIdx = d->findClass(superDesc);
                if (superTypeIdx >= 0) {
                    superDesc = std::string(d->getSuperClassByTypeIdx(
                        static_cast<uint32_t>(superTypeIdx)));
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }
    }

    // 3) Search all DEX files for a method with matching name whose class
    //    descriptor is in the hierarchy.
    for (auto* d : dexFiles_) {
        if (!d) continue;
        for (uint32_t i = 0; i < d->methods.size(); ++i) {
            const auto& method = d->methods.getMethod(i);
            if (method.classIdx >= d->types.size()) continue;
            auto mDesc = std::string(d->getTypeDescriptor(method.classIdx));
            for (const auto& hd : hierarchy) {
                if (mDesc == hd) {
                    std::string name(d->strings.getString(method.nameIdx));
                    if (name == methodName) {
                        outDex = d;
                        return static_cast<int>(i);
                    }
                    break;
                }
            }
        }
    }
    outDex = nullptr;
    return -1;
}

bool ActivityManager::invokeLifecycleMethod(Activity* activity,
                                            const std::string& methodName) {
    if (!activity || !interpreter_) return false;
    
    uint32_t classIdx = activity->getClassIndex();

    // Find which DEX defines this activity class.
    const auto* activityDex = interpreter_->findDexForClass(
        activity->getDescriptor());
    
    const NativeDex::Runtime::DexLoader::DexFile* methodDex = nullptr;
    int methodIdx = findMethodIndex(activityDex, classIdx, methodName, methodDex);
    if (methodIdx < 0) {
        if (interpreter_->isDebug()) {
            std::cerr << "[ActivityManager] Method not found: " << methodName
                      << " on " << activity->getDescriptor() << std::endl;
        }
        return false;
    }
    
    if (interpreter_->isDebug()) {
        std::cout << "[ActivityManager] Invoking " << methodName << " on "
                  << activity->getDescriptor() << std::endl;
    }
    
    interpreter_->executeMethodByIndexInDex(methodDex, static_cast<uint32_t>(methodIdx));
    
    return true;
}

} // namespace NativeDex::Launcher
