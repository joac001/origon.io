#include "sim/CharacterLook.h"

#include <algorithm>
#include <random>

namespace origon {
namespace {

constexpr LookOption kBodies[] = {{"m", "Hombre"}, {"f", "Mujer"}};
constexpr LookOption kHair[] = {{"", "Calvo"},          {"buzz", "Rapado"},          {"short", "Corto"},
                                {"slick", "Peinado hacia atrás"}, {"ponytail", "Cola de caballo"}, {"bun", "Rodete"},
                                {"long", "Largo"}};
constexpr LookOption kFacialHair[] = {{"", "Ninguno"}, {"mustache", "Bigote"}, {"goatee", "Candado"}, {"beard", "Barba"}};
constexpr LookOption kHeadwear[] = {{"", "Nada"},           {"helmet", "Casco aliado"}, {"stahlhelm", "Casco alemán"},
                                    {"cap", "Gorra de campaña"}, {"beret", "Boina"},        {"hat", "Sombrero"},
                                    {"beanie", "Gorro de lana"}};
constexpr LookOption kEyewear[] = {{"", "Nada"},
                                   {"glasses", "Lentes"},
                                   {"sunglasses", "Lentes oscuros"},
                                   {"goggles", "Antiparras"},
                                   {"bandana", "Pañuelo"}};
constexpr LookOption kTops[] = {{"jacket", "Chaqueta de campaña"}, {"shirt", "Camisa arremangada"}, {"tshirt", "Camiseta"},
                                {"sweater", "Suéter"}};
constexpr LookOption kBottoms[] = {{"trousers", "Pantalón"}, {"gaiters", "Pantalón con polainas"}, {"shorts", "Bermuda"}};
constexpr LookOption kBoots[] = {{"short", "Borcegos"}, {"tall", "Botas altas"}};
constexpr LookOption kGear[] = {{"", "Nada"}, {"belt", "Cinturón con cartucheras"}, {"suspenders", "Correaje"},
                                {"backpack", "Mochila"}};

const LookColor kSkinTones[] = {{{0.96f, 0.8f, 0.69f}, "Muy clara"}, {{0.88f, 0.69f, 0.56f}, "Clara"},
                                {{0.78f, 0.57f, 0.44f}, "Media"},    {{0.63f, 0.45f, 0.33f}, "Trigueña"},
                                {{0.45f, 0.31f, 0.22f}, "Morena"},   {{0.3f, 0.2f, 0.15f}, "Oscura"}};
const LookColor kHairColors[] = {{{0.06f, 0.05f, 0.05f}, "Negro"},    {{0.2f, 0.13f, 0.08f}, "Castaño oscuro"},
                                 {{0.38f, 0.25f, 0.14f}, "Castaño"},   {{0.72f, 0.58f, 0.36f}, "Rubio"},
                                 {{0.55f, 0.24f, 0.1f}, "Pelirrojo"},  {{0.62f, 0.6f, 0.58f}, "Canoso"}};
const LookColor kClothColors[] = {{{0.33f, 0.35f, 0.22f}, "Oliva"},       {{0.6f, 0.53f, 0.38f}, "Caqui"},
                                  {{0.36f, 0.38f, 0.35f}, "Gris campaña"}, {{0.38f, 0.27f, 0.18f}, "Marrón"},
                                  {{0.17f, 0.2f, 0.28f}, "Azul marino"},   {{0.72f, 0.65f, 0.5f}, "Arena"},
                                  {{0.2f, 0.3f, 0.2f}, "Verde bosque"},    {{0.12f, 0.12f, 0.12f}, "Negro"}};

template <size_t N>
LookCatalog catalog(const LookOption (&options)[N]) {
    return {options, N};
}

size_t fieldSize(CharacterLook::Field field) {
    size_t colors = 0;
    if (lookColors(field, colors)) return colors;
    return lookOptions(field).count;
}

} // namespace

LookCatalog lookOptions(CharacterLook::Field field) {
    switch (field) {
    case CharacterLook::Body: return catalog(kBodies);
    case CharacterLook::Hair: return catalog(kHair);
    case CharacterLook::FacialHair: return catalog(kFacialHair);
    case CharacterLook::Headwear: return catalog(kHeadwear);
    case CharacterLook::Eyewear: return catalog(kEyewear);
    case CharacterLook::Top: return catalog(kTops);
    case CharacterLook::Bottom: return catalog(kBottoms);
    case CharacterLook::Boots: return catalog(kBoots);
    case CharacterLook::Gear: return catalog(kGear);
    default: return {nullptr, 0};
    }
}

const LookColor* lookColors(CharacterLook::Field field, size_t& count) {
    switch (field) {
    case CharacterLook::SkinTone: count = std::size(kSkinTones); return kSkinTones;
    case CharacterLook::HairColor: count = std::size(kHairColors); return kHairColors;
    case CharacterLook::TopColor:
    case CharacterLook::BottomColor: count = std::size(kClothColors); return kClothColors;
    default: count = 0; return nullptr;
    }
}

const char* lookFieldLabel(CharacterLook::Field field) {
    switch (field) {
    case CharacterLook::Body: return "Cuerpo";
    case CharacterLook::SkinTone: return "Tono de piel";
    case CharacterLook::Hair: return "Pelo";
    case CharacterLook::HairColor: return "Color de pelo";
    case CharacterLook::FacialHair: return "Vello facial";
    case CharacterLook::Headwear: return "Cabeza";
    case CharacterLook::Eyewear: return "Cara";
    case CharacterLook::Top: return "Torso";
    case CharacterLook::TopColor: return "Color del torso";
    case CharacterLook::Bottom: return "Piernas";
    case CharacterLook::BottomColor: return "Color de piernas";
    case CharacterLook::Boots: return "Calzado";
    case CharacterLook::Gear: return "Equipo";
    default: return "";
    }
}

void CharacterLook::sanitize() {
    for (int f = 0; f < FieldCount; ++f) {
        const size_t size = fieldSize(static_cast<Field>(f));
        if (size > 0 && values[static_cast<size_t>(f)] >= size) values[static_cast<size_t>(f)] = 0;
    }
}

CharacterLook CharacterLook::random(uint32_t seed) {
    std::mt19937 rng(seed * 2654435761u + 12345u);
    auto pick = [&](size_t n) { return static_cast<uint8_t>(std::uniform_int_distribution<size_t>(0, n - 1)(rng)); };
    CharacterLook look;
    look[Body] = pick(2);
    look[SkinTone] = pick(std::size(kSkinTones));
    look[Hair] = look.female() ? static_cast<uint8_t>(2 + pick(5)) : pick(5);
    look[HairColor] = pick(std::size(kHairColors));
    look[FacialHair] = look.female() ? 0 : (pick(3) == 0 ? pick(std::size(kFacialHair)) : 0);
    // Soldiers mostly wear helmets.
    const uint8_t headwear = pick(10);
    look[Headwear] = headwear < 5 ? 1 : headwear < 7 ? 2 : static_cast<uint8_t>(3 + pick(4));
    look[Eyewear] = pick(6) == 0 ? static_cast<uint8_t>(1 + pick(std::size(kEyewear) - 1)) : 0;
    look[Top] = pick(std::size(kTops));
    look[TopColor] = pick(4);
    look[Bottom] = pick(std::size(kBottoms) - 1); // Shorts only by choice.
    look[BottomColor] = pick(4);
    look[Boots] = pick(std::size(kBoots));
    look[Gear] = pick(std::size(kGear));
    return look;
}

} // namespace origon
