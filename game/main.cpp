#include "App.h"

#include "core/Log.h"

#include <cstdlib>
#include <exception>
#include <string>

namespace {

void printUsage() {
    std::puts("Usage: origon [--map NAME] [--editor] [--autoplay] [--hide-ui]\n"
              "              [--screenshot FILE.png] [--screenshot-frame N]");
}

} // namespace

int main(int argc, char** argv) {
    origon::LaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (arg == "--map") options.map = next();
        else if (arg == "--editor") options.startInEditor = true;
        else if (arg == "--autoplay") options.autoplay = true;
        else if (arg == "--hide-ui") options.hideUi = true;
        else if (arg == "--screenshot") options.screenshotPath = next();
        else if (arg == "--screenshot-frame") options.screenshotFrame = std::atoi(next().c_str());
        else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            origon::log::warn("Unknown argument: {}", arg);
        }
    }
    try {
        origon::App app(options);
        return app.run();
    } catch (const std::exception& e) {
        origon::log::error("Fatal: {}", e.what());
        return 1;
    }
}
