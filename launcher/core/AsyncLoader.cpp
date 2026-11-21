#include "AsyncLoader.h"
#include "Bootstrap.h"

namespace NativeDex::Launcher {

AsyncLoader::AsyncLoader(CompletionHandler handler)
    : handler_(std::move(handler)) {
}

AsyncLoader::~AsyncLoader() = default;

void AsyncLoader::start() {
    // Placeholder: actual bootstrap should be triggered externally.
}

} // namespace NativeDex::Launcher
