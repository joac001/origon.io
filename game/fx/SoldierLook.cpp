#include "fx/SoldierLook.h"

#include <glm/common.hpp>
#include <glm/exponential.hpp>

#include <cstring>

namespace origon {
namespace {

glm::vec3 linear(const glm::vec3& srgb) { return glm::pow(srgb, glm::vec3(2.2f)); }

const char* optionId(CharacterLook::Field field, const CharacterLook& look) {
    const LookCatalog catalog = lookOptions(field);
    const size_t index = look[field];
    return index < catalog.count ? catalog.options[index].id : "";
}

glm::vec3 color(CharacterLook::Field field, const CharacterLook& look) {
    size_t count = 0;
    const LookColor* colors = lookColors(field, count);
    const size_t index = look[field];
    return colors && index < count ? linear(colors[index].srgb) : glm::vec3(0.5f);
}

bool coversHead(const char* headwear) { return headwear[0] != '\0'; }

} // namespace

SoldierPiece parseSoldierPiece(const std::string& nodeName) {
    SoldierPiece piece;
    const size_t first = nodeName.find('_');
    if (first == std::string::npos || first != 1) {
        piece.slot = nodeName;
        return piece;
    }
    piece.gender = nodeName[0];
    const size_t second = nodeName.find('_', first + 1);
    piece.slot = nodeName.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1);
    if (second != std::string::npos) piece.variant = nodeName.substr(second + 1);
    return piece;
}

bool soldierPieceVisible(const SoldierPiece& piece, const CharacterLook& look) {
    const char gender = look.female() ? 'f' : 'm';
    if (piece.gender != 'x' && piece.gender != gender) return false;
    const std::string& slot = piece.slot;
    const std::string& v = piece.variant;
    const char* top = optionId(CharacterLook::Top, look);
    const char* bottom = optionId(CharacterLook::Bottom, look);
    if (slot == "head" || slot == "hands") return true;
    if (slot == "skin") {
        if (v == "neck") return true;
        if (v == "upperarms") return std::strcmp(top, "tshirt") == 0;
        if (v == "forearms") return std::strcmp(top, "jacket") != 0 && std::strcmp(top, "sweater") != 0;
        if (v == "legs") return std::strcmp(bottom, "shorts") == 0;
        return false; // The torso is always under a top.
    }
    if (slot == "top") return v == top;
    if (slot == "bottom") return v == bottom;
    if (slot == "boots") return v == optionId(CharacterLook::Boots, look);
    if (slot == "gear") return v == optionId(CharacterLook::Gear, look);
    if (slot == "headwear") return v == optionId(CharacterLook::Headwear, look);
    if (slot == "eyewear") return v == optionId(CharacterLook::Eyewear, look);
    if (slot == "face") return !look.female() && v == optionId(CharacterLook::FacialHair, look);
    if (slot == "hair") {
        std::string hair = optionId(CharacterLook::Hair, look);
        // Tall styles would poke through hats and helmets: tuck them into a short cut.
        if (coversHead(optionId(CharacterLook::Headwear, look)) && (hair == "slick" || hair == "bun")) hair = "short";
        return v == hair;
    }
    return false;
}

void applyLookMaterial(Material& material, const std::string& name, const CharacterLook& look,
                       const glm::vec3& playerColorLinear) {
    if (name == "skin") {
        material.albedo = color(CharacterLook::SkinTone, look);
    } else if (name == "lips") {
        material.albedo = color(CharacterLook::SkinTone, look) * glm::vec3(0.78f, 0.6f, 0.58f);
    } else if (name == "hair") {
        material.albedo = color(CharacterLook::HairColor, look);
    } else if (name == "top") {
        material.albedo = color(CharacterLook::TopColor, look);
    } else if (name == "top_detail") {
        material.albedo = color(CharacterLook::TopColor, look) * 0.72f;
    } else if (name == "bottom") {
        material.albedo = color(CharacterLook::BottomColor, look);
    } else if (name == "tint") {
        material.albedo = playerColorLinear;
    }
}

} // namespace origon
