#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace origon {

// One selectable variant: the piece id in soldier.glb ("" = nothing) and its Spanish label.
struct LookOption {
    const char* id;
    const char* label;
};

struct LookColor {
    glm::vec3 srgb;
    const char* label;
};

// Purely cosmetic appearance of a player. It never changes hit volumes, movement or any stat:
// every look uses the same skeleton and the same capsules.
struct CharacterLook {
    enum Field : int {
        Body, SkinTone, Hair, HairColor, FacialHair, Headwear, Eyewear, Top, TopColor, Bottom, BottomColor, Boots, Gear,
        FieldCount
    };
    std::array<uint8_t, FieldCount> values{0, 2, 2, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1};

    uint8_t& operator[](Field f) { return values[static_cast<size_t>(f)]; }
    uint8_t operator[](Field f) const { return values[static_cast<size_t>(f)]; }
    bool female() const { return values[Body] == 1; }

    // Clamps every field to its catalog (data from the network or settings).
    void sanitize();
    // Deterministic random look (bots).
    static CharacterLook random(uint32_t seed);
    bool operator==(const CharacterLook&) const = default;
};

// Catalogs, indexed by the look fields.
struct LookCatalog {
    const LookOption* options;
    size_t count;
};
LookCatalog lookOptions(CharacterLook::Field field);
// Colors for SkinTone, HairColor, TopColor and BottomColor.
const LookColor* lookColors(CharacterLook::Field field, size_t& count);
const char* lookFieldLabel(CharacterLook::Field field);

} // namespace origon
