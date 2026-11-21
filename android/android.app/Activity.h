#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "../../runtime/object_model/Object.h"
#include "../../runtime/object_model/ClassObject.h"
#include "../android.os/Handler.h"
#include "../android.os/Message.h"
#include "../android.os/Looper.h"
#include "../android.content/Intent.h"
#include "../android.content/Context.h"
#include "../android.view/View.h"

namespace NativeDex::Android::App {

using namespace NativeDex::Android::OS;
using namespace NativeDex::Android::Content;
using NativeDex::Android::View::View;

class Activity : public Runtime::ObjectModel::Object {
public:
    // Lifecycle states
    enum class State {
        CREATED,
        STARTED,
        RESUMED,
        PAUSED,
        STOPPED,
        DESTROYED
    };
    
    Activity();
    virtual ~Activity();
    
    // Lifecycle callbacks (override in subclasses)
    virtual void onCreate(Bundle* savedInstanceState) {}
    virtual void onStart() {}
    virtual void onResume() {}
    virtual void onPause() {}
    virtual void onStop() {}
    virtual void onDestroy() {}
    virtual void onRestart() {}
    virtual void onNewIntent(Intent* intent) {}
    virtual void onSaveInstanceState(Bundle* outState) {}
    virtual void onRestoreInstanceState(Bundle* savedInstanceState) {}
    virtual void onConfigurationChanged() {}
    virtual void onLowMemory() {}
    
    // Window
    virtual void setContentView(int layoutResId) {}
    virtual void setContentView(View* view) {}
    virtual View* findViewById(int id) { return nullptr; }
    
    // Intent
    virtual Intent* getIntent() { return nullptr; }
    virtual void setIntent(Intent* intent) {}
    
    // State
    State getState() const { return state_; }
    bool isFinishing() const { return finishing_; }
    bool isDestroyed() const { return destroyed_; }
    
    // Finish activity
    virtual void finish() { finishing_ = true; }
    
    // Context
    virtual Context* getApplicationContext() { return nullptr; }
    
protected:
    State state_ = State::CREATED;
    bool finishing_ = false;
    bool destroyed_ = false;
    Handler* mainHandler_ = nullptr;
    Intent* intent_ = nullptr;
};

} // namespace NativeDex::Android::App