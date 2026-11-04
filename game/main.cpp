#include "App.h"

#include "core/Log.h"

#include <cstdlib>
#include <exception>
#include <string>

namespace {

void printUsage() {
    std::puts("Usage: origon [--map NAME] [--editor] [--autoplay] [--hide-ui]\n"
              "              [--screenshot FILE.png] [--screenshot-frame N] [--shot-size WxH]\n"
              "              [--camera X Y Z YAW PITCH] [--host [PORT]] [--connect HOST[:PORT]] [--name NAME]\n"
              "              [--relay HOST[:PORT]] [--publish] [--join-code CODE]");
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
        else if (arg == "--title") options.forceTitle = true;
        else if (arg == "--follow") options.followPlayer = std::atoi(next().c_str());
        else if (arg == "--pose-gallery") options.poseGallery = true;
        else if (arg == "--force-lod") options.forceLod = std::atoi(next().c_str());
        else if (arg == "--bots") options.botCount = std::atoi(next().c_str());
        else if (arg == "--bench") options.benchSeconds = std::stof(next());
        else if (arg == "--press-play") options.pressPlayFrame = std::atoi(next().c_str());
        else if (arg == "--look-gallery") options.lookGallery = std::atoi(next().c_str());
        else if (arg == "--dump-sounds") options.dumpSounds = next();
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
        else if (arg == "--host") {
            options.hostPort = origon::net::kDefaultPort;
            if (i + 1 < argc && argv[i + 1][0] != '-') options.hostPort = std::atoi(argv[++i]);
        } else if (arg == "--connect") {
            std::string target = next();
            const size_t colon = target.find(':');
            if (colon != std::string::npos) {
                options.connectPort = std::atoi(target.substr(colon + 1).c_str());
                target = target.substr(0, colon);
            }
            options.connectAddress = target;
        } else if (arg == "--name") {
            options.playerName = next();
        } else if (arg == "--relay") {
            options.relayAddress = next();
        } else if (arg == "--publish") {
            options.publish = true;
        } else if (arg == "--join-code") {
            options.joinCode = next();
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
