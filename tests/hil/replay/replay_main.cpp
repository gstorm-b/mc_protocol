// The doctest main of mc_replay_tests: reads --replay-root (replay_options.h), then runs doctest.
// Named replay_main.cpp (not main.cpp) so that no build tool confuses it with another main.cpp.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include "replay_options.h"

#include <cstdlib>
#include <string>

namespace mc::replay {
std::string g_rootOption;
} // namespace mc::replay

int main(int argc, char** argv) {
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const std::string prefix = "--replay-root=";
        if (arg.compare(0, prefix.size(), prefix) == 0) {
            mc::replay::g_rootOption = arg.substr(prefix.size());
        } else if (arg == "--replay-root" && i + 1 < argc) {
            mc::replay::g_rootOption = argv[++i];
        }
    }
    if (mc::replay::g_rootOption.empty()) {
#ifdef _MSC_VER
        char* env = nullptr;
        size_t length = 0;
        if (_dupenv_s(&env, &length, "MC_REPLAY_ROOT") == 0 && env != nullptr) {
            mc::replay::g_rootOption = env;
            std::free(env);
        }
#else
        if (const char* env = std::getenv("MC_REPLAY_ROOT")) {
            mc::replay::g_rootOption = env;
        }
#endif
    }
    return context.run();
}
