#pragma once

#include "render/Renderer.h"
#include "sim/CharacterLook.h"

#include <glm/vec3.hpp>

#include <string>

namespace origon {

// Maps a CharacterLook onto the pieces of soldier.glb (tools/blender/character_parts.py).
// Piece names are "<g>_<slot>_<variant>" with g = m (male), f (female) or x (shared).
struct SoldierPiece {
    char gender = 'x';
    std::string slot;
    std::string variant;
};

SoldierPiece parseSoldierPiece(const std::string& nodeName);
bool soldierPieceVisible(const SoldierPiece& piece, const CharacterLook& look);
// Recolors a piece material for the look (skin tone, hair, clothing colors, player armband).
void applyLookMaterial(Material& material, const std::string& materialName, const CharacterLook& look,
                       const glm::vec3& playerColorLinear);

} // namespace origon
