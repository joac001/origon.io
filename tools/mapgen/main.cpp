// Writes generated maps to JSON (the built-in maps live in maps/).
// Usage: origon_mapgen bosque|aserradero [output.json]
//        origon_mapgen procedural SEED [output.json]
#include "core/FileSystem.h"
#include "core/Log.h"
#include "world/MapGenerator.h"

#include <cstdlib>
#include <string>

using namespace origon;

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "bosque";
    MapData map;
    std::string output;
    if (which == "procedural") {
        const auto seed = static_cast<uint32_t>(argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 1);
        map = generateProceduralMap(seed);
        output = argc > 3 ? argv[3] : (fs::resolve("maps") / ("procedural_" + std::to_string(seed) + ".json")).string();
    } else if (which == "bosque" || which == "aserradero") {
        map = which == "bosque" ? generateForestMap() : generateSawmillMap();
        output = argc > 2 ? argv[2] : (fs::resolve("maps") / (which + ".json")).string();
    } else {
        log::error("Unknown map '{}'", which);
        return 1;
    }
    return map.save(output) ? 0 : 1;
}
