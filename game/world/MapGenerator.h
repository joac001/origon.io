#pragma once

#include "world/MapData.h"

#include <cstdint>

namespace origon {

// Built-in maps, generated from code so they stay reproducible (tools/mapgen writes them to maps/).
MapData generateForestMap();  // "Bosque": creek, ruined cabin, bridge, camp.
MapData generateSawmillMap(); // "Aserradero": lakeside logging camp at late afternoon.

// A new map from a seed: terrain, water (creek, lake or none), structures placed by rules, paths,
// vegetation and validated spawns. Same seed, same map on every machine.
MapData generateProceduralMap(uint32_t seed);

} // namespace origon
