#include "ui/Theme.h"

#include "core/FileSystem.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace origon::ui {
namespace {

Fonts g_fonts;

ImVec4 toVec4(ImU32 c, float alpha = -1.0f) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    if (alpha >= 0.0f) v.w = alpha;
    return v;
}

ImFont* addFont(const char* file, float size) {
    ImGuiIO& io = ImGui::GetIO();
    const std::string path = fs::resolve(std::string("assets/fonts/") + file).string();
    ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), size);
    if (!font) return nullptr;
    // DejaVu fills in the symbols Barlow does not have (∞, arrows, ✓...).
    ImFontConfig merge;
    merge.MergeMode = true;
    const std::string fallback = fs::resolve("assets/fonts/DejaVuSans.ttf").string();
    io.Fonts->AddFontFromFileTTF(fallback.c_str(), size, &merge);
    return font;
}

// Length in bytes of the UTF-8 sequence starting with this byte.
int utf8Length(unsigned char c) { return c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1; }

} // namespace

Fonts loadFontsAndStyle() {
    g_fonts.body = addFont("Barlow-Regular.ttf", 18.0f);
    g_fonts.medium = addFont("Barlow-Medium.ttf", 18.0f);
    g_fonts.display = addFont("BarlowCondensed-SemiBold.ttf", 18.0f);
    g_fonts.heavy = addFont("BarlowCondensed-Bold.ttf", 18.0f);
    g_fonts.label = addFont("BarlowCondensed-Medium.ttf", 18.0f);
    ImFont* fallback = ImGui::GetIO().Fonts->AddFontDefault();
    for (ImFont** f : {&g_fonts.body, &g_fonts.medium, &g_fonts.display, &g_fonts.heavy, &g_fonts.label}) {
        if (!*f) *f = fallback;
    }
    ImGui::GetIO().FontDefault = g_fonts.body;

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 2.0f;
    style.ChildRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(22, 18);
    style.FramePadding = ImVec2(10, 7);
    style.ItemSpacing = ImVec2(10, 9);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 10.0f;
    style.TabBarBorderSize = 1.0f;
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = toVec4(color::kPaper);
    c[ImGuiCol_TextDisabled] = toVec4(color::kDim);
    c[ImGuiCol_WindowBg] = toVec4(color::kInk, 0.86f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = toVec4(color::kInk, 0.97f);
    c[ImGuiCol_Border] = toVec4(color::kLine);
    c[ImGuiCol_Separator] = toVec4(color::kLine);
    c[ImGuiCol_FrameBg] = ImVec4(1.0f, 0.98f, 0.9f, 0.06f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(1.0f, 0.98f, 0.9f, 0.1f);
    c[ImGuiCol_FrameBgActive] = ImVec4(1.0f, 0.98f, 0.9f, 0.14f);
    c[ImGuiCol_TitleBg] = toVec4(color::kInk, 0.95f);
    c[ImGuiCol_TitleBgActive] = toVec4(color::kInk, 0.95f);
    c[ImGuiCol_Button] = ImVec4(1.0f, 0.98f, 0.9f, 0.07f);
    c[ImGuiCol_ButtonHovered] = toVec4(color::kBrass, 0.28f);
    c[ImGuiCol_ButtonActive] = toVec4(color::kBrass, 0.45f);
    c[ImGuiCol_Header] = toVec4(color::kBrass, 0.18f);
    c[ImGuiCol_HeaderHovered] = toVec4(color::kBrass, 0.28f);
    c[ImGuiCol_HeaderActive] = toVec4(color::kBrass, 0.4f);
    c[ImGuiCol_CheckMark] = toVec4(color::kBrass);
    c[ImGuiCol_SliderGrab] = toVec4(color::kBrass);
    c[ImGuiCol_SliderGrabActive] = toVec4(color::kPaper);
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = toVec4(color::kBrass, 0.22f);
    c[ImGuiCol_TabSelected] = toVec4(color::kBrass, 0.16f);
    c[ImGuiCol_TabSelectedOverline] = toVec4(color::kBrass);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1.0f, 0.98f, 0.9f, 0.14f);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_NavCursor] = toVec4(color::kBrass);
    return g_fonts;
}

const Fonts& fonts() { return g_fonts; }

float scale() { return std::max(0.5f, ImGui::GetIO().DisplaySize.y / 1080.0f); }

ImU32 withAlpha(ImU32 c, float alpha) {
    const auto a = static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFF));
    return (c & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

ImVec2 measure(ImFont* font, float size, const std::string& s, float tracking) {
    ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str());
    if (tracking != 0.0f && !s.empty()) {
        int glyphs = 0;
        for (size_t i = 0; i < s.size(); i += static_cast<size_t>(utf8Length(static_cast<unsigned char>(s[i])))) ++glyphs;
        extent.x += tracking * static_cast<float>(glyphs - 1);
    }
    return extent;
}

void text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 c, const std::string& s, float tracking, bool shadow) {
    const float alpha = static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
    auto draw = [&](ImVec2 at, ImU32 col) {
        if (tracking == 0.0f) {
            dl->AddText(font, size, at, col, s.c_str());
            return;
        }
        float x = at.x;
        for (size_t i = 0; i < s.size();) {
            const size_t n = static_cast<size_t>(utf8Length(static_cast<unsigned char>(s[i])));
            const char* begin = s.c_str() + i;
            dl->AddText(font, size, ImVec2(x, at.y), col, begin, begin + n);
            x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, begin + n).x + tracking;
            i += n;
        }
    };
    if (shadow) {
        const float offset = std::max(1.0f, size / 22.0f);
        draw(ImVec2(pos.x, pos.y + offset), withAlpha(color::kShadow, alpha));
    }
    draw(pos, c);
}

void textAligned(ImDrawList* dl, ImFont* font, float size, ImVec2 anchor, ImVec2 align, ImU32 c, const std::string& s,
                 float tracking, bool shadow) {
    const ImVec2 extent = measure(font, size, s, tracking);
    text(dl, font, size, ImVec2(std::floor(anchor.x - extent.x * align.x), std::floor(anchor.y - extent.y * align.y)), c, s,
         tracking, shadow);
}

void label(ImDrawList* dl, ImVec2 pos, ImU32 c, const std::string& s, float size, ImVec2 align) {
    const float k = scale();
    textAligned(dl, g_fonts.label, size * k, pos, align, c, upper(s), size * k * 0.14f, true);
}

void panel(ImDrawList* dl, ImVec2 min, ImVec2 max, float opacity, bool border) {
    dl->AddRectFilled(min, max, withAlpha(color::kInk, opacity), 2.0f * scale());
    if (border) dl->AddRect(min, max, color::kLine, 2.0f * scale(), 0, 1.0f);
}

std::string upper(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            // Latin-1 supplement lowercase (U+00E0..U+00FE, except U+00F7) -> uppercase is 0x20 lower.
            const auto d = static_cast<unsigned char>(s[i + 1]);
            out += static_cast<char>(c);
            out += static_cast<char>(d >= 0xA0 && d <= 0xBE && d != 0xB7 ? d - 0x20 : d);
            ++i;
        } else {
            out += static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        }
    }
    return out;
}

} // namespace origon::ui
