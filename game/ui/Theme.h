#pragma once

#include <imgui.h>

#include <string>

namespace origon::ui {

// Visual language of the menus and the HUD: a 1940s field palette (olive drab, sand, brass) drawn
// the modern way (flat translucent panels, hairlines, condensed type with generous spacing, one
// accent color). Sizes are in pixels at 1080p; multiply by scale().
namespace color {
constexpr ImU32 kInk = IM_COL32(12, 14, 11, 255);        // Panel base.
constexpr ImU32 kPaper = IM_COL32(237, 232, 218, 255);   // Primary text.
constexpr ImU32 kMuted = IM_COL32(170, 166, 148, 255);   // Secondary text.
constexpr ImU32 kDim = IM_COL32(112, 110, 98, 255);      // Tertiary text, inactive marks.
constexpr ImU32 kLine = IM_COL32(237, 232, 218, 38);     // Hairlines and separators.
constexpr ImU32 kBrass = IM_COL32(222, 168, 70, 255);    // The accent.
constexpr ImU32 kOlive = IM_COL32(128, 146, 84, 255);    // Positive (promotion, protection).
constexpr ImU32 kDanger = IM_COL32(222, 86, 72, 255);    // Damage, low health, demotion.
constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 150);
} // namespace color

struct Fonts {
    ImFont* body = nullptr;     // Barlow Regular.
    ImFont* medium = nullptr;   // Barlow Medium.
    ImFont* display = nullptr;  // Barlow Condensed SemiBold: titles, numbers.
    ImFont* heavy = nullptr;    // Barlow Condensed Bold: hero numbers.
    ImFont* label = nullptr;    // Barlow Condensed Medium: small caps labels.
};

// Loads the fonts (with DejaVu merged in for symbols Barlow lacks) and applies the ImGui style.
Fonts loadFontsAndStyle();
const Fonts& fonts();

float scale(); // Display height / 1080.
ImU32 withAlpha(ImU32 color, float alpha);
ImU32 mix(ImU32 a, ImU32 b, float t);

ImVec2 measure(ImFont* font, float size, const std::string& text, float tracking = 0.0f);
// Text with a soft drop shadow (readable over any scene). tracking: extra pixels between letters.
void text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const std::string& s, float tracking = 0.0f,
          bool shadow = true);
// Anchored text: align.x / align.y in 0..1 of the text box (0.5 = centered, 1 = right/bottom).
void textAligned(ImDrawList* dl, ImFont* font, float size, ImVec2 anchor, ImVec2 align, ImU32 color, const std::string& s,
                 float tracking = 0.0f, bool shadow = true);
// Small uppercase label (tracked condensed type).
void label(ImDrawList* dl, ImVec2 pos, ImU32 color, const std::string& s, float size = 14.5f, ImVec2 align = ImVec2(0, 0));
// Flat translucent panel with a hairline border.
void panel(ImDrawList* dl, ImVec2 min, ImVec2 max, float opacity = 0.72f, bool border = true);
std::string upper(const std::string& s); // UTF-8 aware for Spanish letters (á, é, í, ó, ú, ñ, ü).

} // namespace origon::ui
