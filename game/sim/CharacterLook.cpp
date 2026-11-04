#include "sim/CharacterLook.h"

#include <algorithm>
#include <random>

namespace origon {
namespace {

// Ids match the pieces of soldier_<m|f>.glb (tools/blender/gen_human.py).
constexpr LookOption kBodies[] = {{"m", "Hombre"}, {"f", "Mujer"}};
constexpr LookOption kSkins[] = {{"young_caucasian", "Clara"},           {"young_asian", "Media"},
                                 {"young_african", "Oscura"},            {"middleage_caucasian", "Clara, madura"},
                                 {"middleage_asian", "Media, madura"},   {"middleage_african", "Oscura, madura"}};
constexpr LookOption kHair[] = {{"", "Calvo"},          {"short04", "Rapado"},   {"short02", "Corto"},
                                {"short01", "Corto despeinado"}, {"short03", "Corto con raya"}, {"afro01", "Afro"},
                                {"bob02", "Melena corta"},  {"bob01", "Melena"},     {"long01", "Largo"},
                                {"ponytail01", "Cola de caballo"}, {"braid01", "Trenza"}};
constexpr LookOption kFacialHair[] = {{"", "Ninguno"}, {"mustache", "Bigote"}, {"goatee", "Candado"}, {"beard", "Barba"}};
constexpr LookOption kHeadwear[] = {{"", "Nada"},           {"helmet", "Casco aliado"}, {"stahlhelm", "Casco alemán"},
                                    {"cap", "Gorra de campaña"}, {"beret", "Boina"},        {"fedora", "Sombrero fedora"},
                                    {"hat", "Sombrero de ala ancha"}, {"beanie", "Gorro de lana"}};
constexpr LookOption kEyewear[] = {{"", "Nada"},
                                   {"glasses", "Lentes"},
                                   {"sunglasses", "Lentes oscuros"},
                                   {"goggles", "Antiparras"},
                                   {"bandana", "Pañuelo"}};
constexpr LookOption kMaleOutfits[] = {{"male_worksuit01", "Mameluco de trabajo"}, {"male_casualsuit01", "Campera y jean"},
                                       {"male_casualsuit02", "Camisa y pantalón"}, {"male_casualsuit03", "Buzo y pantalón"},
                                       {"male_casualsuit04", "Remera y jean"},     {"male_casualsuit05", "Chaleco y camisa"},
                                       {"male_casualsuit06", "Camisa a cuadros"}};
constexpr LookOption kFemaleOutfits[] = {{"male_worksuit01", "Mameluco de trabajo"}, {"female_casualsuit01", "Remera y jean"},
                                         {"female_casualsuit02", "Campera y pantalón"}, {"female_sportsuit01", "Deportivo"}};
constexpr LookOption kShoes[] = {{"shoes01", "Borcegos"},        {"shoes02", "Zapatillas"},     {"shoes03", "Botas"},
                                 {"shoes04", "Borcegos oscuros"}, {"shoes05", "Zapatillas blancas"}, {"shoes06", "Zapatos"}};

const LookColor kHairColors[] = {{{0.08f, 0.07f, 0.06f}, "Negro"},    {{0.24f, 0.16f, 0.1f}, "Castaño oscuro"},
                                 {{0.45f, 0.3f, 0.18f}, "Castaño"},    {{0.85f, 0.7f, 0.45f}, "Rubio"},
                                 {{0.62f, 0.28f, 0.12f}, "Pelirrojo"}, {{0.7f, 0.68f, 0.66f}, "Canoso"}};
const LookColor kOutfitColors[] = {{{1.0f, 1.0f, 1.0f}, "Original"},     {{0.42f, 0.45f, 0.3f}, "Oliva"},
                                   {{0.72f, 0.64f, 0.48f}, "Caqui"},     {{0.46f, 0.48f, 0.45f}, "Gris campaña"},
                                   {{0.48f, 0.36f, 0.25f}, "Marrón"},    {{0.26f, 0.3f, 0.4f}, "Azul marino"},
                                   {{0.3f, 0.4f, 0.3f}, "Verde bosque"}, {{0.2f, 0.2f, 0.2f}, "Negro"}};

template <size_t N>
LookCatalog catalog(const LookOption (&options)[N]) {
    return {options, N};
}

size_t fieldSize(CharacterLook::Field field, bool female) {
    size_t colors = 0;
    if (lookColors(field, colors)) return colors;
    return lookOptions(field, female).count;
}

} // namespace

LookCatalog lookOptions(CharacterLook::Field field, bool female) {
    switch (field) {
    case CharacterLook::Body: return catalog(kBodies);
    case CharacterLook::SkinTone: return catalog(kSkins);
    case CharacterLook::Hair: return catalog(kHair);
    case CharacterLook::FacialHair: return catalog(kFacialHair);
    case CharacterLook::Headwear: return catalog(kHeadwear);
    case CharacterLook::Eyewear: return catalog(kEyewear);
    case CharacterLook::Outfit: return female ? catalog(kFemaleOutfits) : catalog(kMaleOutfits);
    case CharacterLook::Shoes: return catalog(kShoes);
    default: return {nullptr, 0};
    }
}

const LookColor* lookColors(CharacterLook::Field field, size_t& count) {
    switch (field) {
    case CharacterLook::HairColor: count = std::size(kHairColors); return kHairColors;
    case CharacterLook::OutfitColor: count = std::size(kOutfitColors); return kOutfitColors;
    default: count = 0; return nullptr;
    }
}

const char* lookFieldLabel(CharacterLook::Field field) {
    switch (field) {
    case CharacterLook::Body: return "Cuerpo";
    case CharacterLook::SkinTone: return "Piel";
    case CharacterLook::Hair: return "Pelo";
    case CharacterLook::HairColor: return "Color de pelo";
    case CharacterLook::FacialHair: return "Vello facial";
    case CharacterLook::Headwear: return "Cabeza";
    case CharacterLook::Eyewear: return "Cara";
    case CharacterLook::Outfit: return "Ropa";
    case CharacterLook::OutfitColor: return "Color de la ropa";
    case CharacterLook::Shoes: return "Calzado";
    default: return "";
    }
}

void CharacterLook::sanitize() {
    if (values[Body] > 1) values[Body] = 0;
    for (int f = 0; f < FieldCount; ++f) {
        const size_t size = fieldSize(static_cast<Field>(f), female());
        if (size > 0 && values[static_cast<size_t>(f)] >= size) values[static_cast<size_t>(f)] = 0;
    }
}

CharacterLook CharacterLook::random(uint32_t seed) {
    std::mt19937 rng(seed * 2654435761u + 12345u);
    auto pick = [&](size_t n) { return static_cast<uint8_t>(std::uniform_int_distribution<size_t>(0, n - 1)(rng)); };
    CharacterLook look;
    look[Body] = pick(2);
    const bool female = look.female();
    look[SkinTone] = pick(std::size(kSkins));
    // Short styles for men, any for women.
    look[Hair] = female ? static_cast<uint8_t>(2 + pick(std::size(kHair) - 2)) : pick(5);
    look[HairColor] = pick(std::size(kHairColors));
    look[FacialHair] = female ? 0 : (pick(3) == 0 ? pick(std::size(kFacialHair)) : 0);
    // Soldiers mostly wear helmets.
    const uint8_t headwear = pick(10);
    look[Headwear] = headwear < 5 ? 1 : headwear < 7 ? 2 : static_cast<uint8_t>(3 + pick(std::size(kHeadwear) - 3));
    look[Eyewear] = pick(6) == 0 ? static_cast<uint8_t>(1 + pick(std::size(kEyewear) - 1)) : 0;
    look[Outfit] = pick(female ? std::size(kFemaleOutfits) : std::size(kMaleOutfits));
    look[OutfitColor] = pick(std::size(kOutfitColors));
    look[Shoes] = pick(std::size(kShoes));
    return look;
}

} // namespace origon
