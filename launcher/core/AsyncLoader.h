#pragma once

#include <functional>

namespace NativeDex::Launcher {
    struct BootstrapResult;

    class AsyncLoader {
    public:
        using CompletionHandler = std::function<void(const BootstrapResult&)>;
        explicit AsyncLoader(CompletionHandler handler);
        ~AsyncLoader();

        void start();

    private:
        CompletionHandler handler_;
    };
}
