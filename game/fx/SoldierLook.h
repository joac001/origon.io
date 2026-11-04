#pragma once

#include "render/Renderer.h"
#include "sim/CharacterLook.h"

#include <glm/vec3.hpp>

#include <string>
#include <vector>

namespace origon {

// Maps a CharacterLook onto the pieces of soldier.glb (tools/blender/character_parts.py).
// Piece names are "x_<slot>[_<variant>]"; male and female bodies live in separate files.
struct SoldierPiece {
    char gender = 'x';
    std::string slot;
    std::string variant;
    std::vector<std::string> hiddenBy; // Skin parts: outfits/shoes that cover them (soldier_*.pieces.json).
};

SoldierPiece parseSoldierPiece(const std::string& nodeName);
bool soldierPieceVisible(const SoldierPiece& piece, const CharacterLook& look);
// Recolors a piece material for the look (skin tone, hair, clothing colors, player armband).
void applyLookMaterial(Material& material, const std::string& materialName, const CharacterLook& look,
                       const glm::vec3& playerColorLinear, class TextureCache& textures);

} // namespace origon
