#include "App.h"

#include "core/Log.h"

#include <cstdlib>
#include <exception>
#include <string>

namespace {

void printUsage() {
    std::puts("Usage: origon [--map NAME] [--editor] [--autoplay] [--hide-ui]\n"
              "              [--screenshot FILE.png] [--screenshot-frame N] [--shot-size WxH]\n"
              "              [--camera X Y Z YAW PITCH]");
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
        else if (arg == "--camera") {
            options.fixedCamera = true;
            options.cameraPosition.x = std::stof(next());
            options.cameraPosition.y = std::stof(next());
            options.cameraPosition.z = std::stof(next());
            options.cameraYaw = std::stof(next());
            options.cameraPitch = std::stof(next());
        } else if (arg == "--shot-size") {
            const std::string size = next();
            const size_t x = size.find('x');
            if (x != std::string::npos) {
                options.shotSize = {std::atoi(size.substr(0, x).c_str()), std::atoi(size.substr(x + 1).c_str())};
            }
        }
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
