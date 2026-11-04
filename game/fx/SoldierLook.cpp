#include "fx/SoldierLook.h"

#include "render/Texture.h"

#include <glm/common.hpp>
#include <glm/exponential.hpp>

#include <cstring>
#include <string>

namespace origon {
namespace {

glm::vec3 linear(const glm::vec3& srgb) { return glm::pow(srgb, glm::vec3(2.2f)); }

const char* optionId(CharacterLook::Field field, const CharacterLook& look) {
    const LookCatalog catalog = lookOptions(field, look.female());
    const size_t index = look[field];
    return index < catalog.count ? catalog.options[index].id : "";
}

glm::vec3 color(CharacterLook::Field field, const CharacterLook& look) {
    size_t count = 0;
    const LookColor* colors = lookColors(field, count);
    const size_t index = look[field];
    return colors && index < count ? colors[index].srgb : glm::vec3(1.0f);
}

} // namespace

SoldierPiece parseSoldierPiece(const std::string& fullName) {
    SoldierPiece piece;
    std::string nodeName = fullName;
    if (const size_t suffix = nodeName.rfind("_LOD"); suffix != std::string::npos && suffix + 4 < nodeName.size()) {
        piece.lod = nodeName[suffix + 4] - '0';
        nodeName = nodeName.substr(0, suffix);
    }
    if (nodeName.size() < 3 || nodeName[1] != '_') {
        piece.slot = nodeName;
        return piece;
    }
    piece.gender = nodeName[0];
    const size_t second = nodeName.find('_', 2);
    piece.slot = nodeName.substr(2, second == std::string::npos ? std::string::npos : second - 2);
    if (second != std::string::npos) piece.variant = nodeName.substr(second + 1);
    return piece;
}

bool soldierPieceVisible(const SoldierPiece& piece, const CharacterLook& look, int lod) {
    if (piece.lod != lod) return false;
    const std::string& slot = piece.slot;
    const std::string& v = piece.variant;
    if (slot == "skin") {
        const std::string outfit = optionId(CharacterLook::Outfit, look);
        const std::string shoes = optionId(CharacterLook::Shoes, look);
        for (const std::string& item : piece.hiddenBy) {
            if (item == outfit || item == shoes) return false;
        }
        return true;
    }
    if (slot == "body" || slot == "eyes" || slot == "eyebrows" || slot == "eyelashes") return true;
    if (slot == "outfit") return v == optionId(CharacterLook::Outfit, look);
    if (slot == "shoes") return v == optionId(CharacterLook::Shoes, look);
    if (slot == "headwear") return v == optionId(CharacterLook::Headwear, look);
    if (slot == "eyewear") return v == optionId(CharacterLook::Eyewear, look);
    if (slot == "face") return !look.female() && v == optionId(CharacterLook::FacialHair, look);
    if (slot == "hair") {
        std::string hair = optionId(CharacterLook::Hair, look);
        // Voluminous styles would poke through hats and helmets: tuck them into a short cut.
        if (optionId(CharacterLook::Headwear, look)[0] != '\0' && (hair == "afro01" || hair == "bob01" || hair == "short01")) {
            hair = "short02";
        }
        return v == hair;
    }
    return false;
}

void applyLookMaterial(Material& material, const std::string& name, const CharacterLook& look,
                       const glm::vec3& playerColorLinear, TextureCache& textures) {
    if (name == "char_skin") {
        const std::string file = std::string("assets/textures/characters/skin_") + optionId(CharacterLook::SkinTone, look) +
                                 (look.female() ? "_female.jpg" : "_male.jpg");
        material.albedoMap = textures.load(file, true).id;
        material.albedo = glm::vec3(1.0f);
        material.roughness = 0.55f;
    } else if (name.rfind("charalpha_hair_", 0) == 0) {
        // Hair textures are exported grayscale: the look's hair color tints them.
        material.albedo = linear(color(CharacterLook::HairColor, look)) * 1.6f;
    } else if (name == "hair") {
        material.albedo = linear(color(CharacterLook::HairColor, look));
    } else if (name.rfind("char_male_", 0) == 0 || name.rfind("char_female_", 0) == 0) {
        if (look[CharacterLook::OutfitColor] != 0) {
            material.albedo = linear(color(CharacterLook::OutfitColor, look)) * 2.2f;
        }
    } else if (name == "tint") {
        material.albedo = playerColorLinear;
    }
}

} // namespace origon
