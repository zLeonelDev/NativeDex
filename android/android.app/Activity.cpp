#include "Activity.h"
#include "../android.os/Looper.h"
#include "../android.os/Handler.h"

namespace NativeDex::Android::App {

Activity::Activity() {
    mainHandler_ = new Handler(Looper::getMainLooper());
}

Activity::~Activity() {
    delete mainHandler_;
}

} // namespace NativeDex::Android::App