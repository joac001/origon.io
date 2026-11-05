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
    const float px = size * k * 1.15f; // Uppercase condensed reads small: optical boost.
    textAligned(dl, g_fonts.label, px, pos, align, c, upper(s), px * 0.14f, true);
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

bool menuItem(const char* id, const std::string& s, float size, bool enabled) {
    const float k = scale();
    const std::string caption = upper(s);
    const float tracking = size * k * 0.06f;
    const ImVec2 extent = measure(g_fonts.display, size * k, caption, tracking);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 box(std::max(extent.x + 40.0f * k, ImGui::GetContentRegionAvail().x), extent.y + 10.0f * k);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(id, box);
    ImGui::EndDisabled();
    const bool hot = enabled && (ImGui::IsItemHovered() || ImGui::IsItemFocused());
    // Ease the highlight in/out per item.
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetItemID();
    float t = storage->GetFloat(key, 0.0f);
    t += ((hot ? 1.0f : 0.0f) - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
    storage->SetFloat(key, t);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float slide = 12.0f * k * t;
    if (t > 0.01f) {
        dl->AddRectFilled(ImVec2(pos.x, pos.y + box.y * 0.5f - 1.5f * k), ImVec2(pos.x + 18.0f * k * t, pos.y + box.y * 0.5f + 1.5f * k),
                          withAlpha(color::kBrass, t));
    }
    const ImU32 c = !enabled ? color::kDim : mix(color::kPaper, color::kBrass, t);
    text(dl, g_fonts.display, size * k, ImVec2(pos.x + slide + (t > 0.01f ? 10.0f * k * t : 0.0f), pos.y + 5.0f * k), c, caption,
         tracking);
    return pressed;
}

int selector(const char* id, const std::string& caption, const std::string& value, float width) {
    const float k = scale();
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float height = 44.0f * k;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int result = 0;
    const float arrow = 34.0f * k;
    ImGui::InvisibleButton("##body", ImVec2(width, height));
    const bool focused = ImGui::IsItemFocused() || ImGui::IsItemHovered();
    if (ImGui::IsItemFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) result = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) result = 1;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool inside = ImGui::IsItemHovered();
    const bool overLeft = inside && mouse.x < pos.x + arrow, overRight = inside && mouse.x > pos.x + width - arrow;
    if (ImGui::IsItemClicked()) result = overLeft ? -1 : overRight ? 1 : result;
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(237, 232, 218, focused ? 22 : 12), 2.0f * k);
    dl->AddRectFilled(ImVec2(pos.x, pos.y + height - 2.0f * k), ImVec2(pos.x + width, pos.y + height),
                      focused ? color::kBrass : color::kLine);
    label(dl, ImVec2(pos.x, pos.y - 20.0f * k), color::kMuted, caption, 13.5f);
    textAligned(dl, g_fonts.display, 32.0f * k, ImVec2(pos.x + arrow * 0.5f, pos.y + height * 0.5f), ImVec2(0.5f, 0.5f),
                overLeft ? color::kBrass : color::kMuted, "‹", 0.0f, false);
    textAligned(dl, g_fonts.display, 32.0f * k, ImVec2(pos.x + width - arrow * 0.5f, pos.y + height * 0.5f), ImVec2(0.5f, 0.5f),
                overRight ? color::kBrass : color::kMuted, "›", 0.0f, false);
    textAligned(dl, g_fonts.display, 22.0f * k, ImVec2(pos.x + width * 0.5f, pos.y + height * 0.5f), ImVec2(0.5f, 0.5f), color::kPaper,
                upper(value), 1.0f * k, false);
    ImGui::PopID();
    return result;
}

namespace {
constexpr float kCaptionShare = 0.42f; // Fraction of the row width for captions.
}

int stepper(const char* id, const std::string& caption, const std::string& value, const ImVec4* swatch) {
    const float k = scale();
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 38.0f * k;
    ImGui::InvisibleButton("##row", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered(), focused = ImGui::IsItemFocused();
    int result = 0;
    if (focused) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) result = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) result = 1;
    }
    const float controlX = pos.x + width * kCaptionShare;
    const float arrow = 30.0f * k;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool overLeft = hovered && mouse.x >= controlX && mouse.x < controlX + arrow;
    const bool overRight = hovered && mouse.x > pos.x + width - arrow;
    if (ImGui::IsItemClicked()) result = overLeft ? -1 : overRight ? 1 : (mouse.x >= controlX ? 1 : 0);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered || focused) dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(237, 232, 218, 10));
    const float mid = pos.y + height * 0.5f;
    textAligned(dl, g_fonts.body, 18.0f * k, ImVec2(pos.x + 2.0f * k, mid), ImVec2(0.0f, 0.5f), color::kMuted, caption, 0.0f, false);
    dl->AddRectFilled(ImVec2(controlX, pos.y + 3.0f * k), ImVec2(pos.x + width, pos.y + height - 3.0f * k), IM_COL32(237, 232, 218, 14));
    dl->AddRectFilled(ImVec2(controlX, pos.y + height - 4.0f * k), ImVec2(pos.x + width, pos.y + height - 3.0f * k),
                      focused || hovered ? color::kBrass : color::kLine);
    textAligned(dl, g_fonts.display, 28.0f * k, ImVec2(controlX + arrow * 0.5f, mid), ImVec2(0.5f, 0.55f),
                overLeft ? color::kBrass : color::kDim, "‹", 0.0f, false);
    textAligned(dl, g_fonts.display, 28.0f * k, ImVec2(pos.x + width - arrow * 0.5f, mid), ImVec2(0.5f, 0.55f),
                overRight ? color::kBrass : color::kDim, "›", 0.0f, false);
    const float center = (controlX + pos.x + width) * 0.5f;
    if (swatch) {
        const ImVec2 extent = measure(g_fonts.medium, 18.0f * k, value);
        const float sx = center - extent.x * 0.5f - 18.0f * k;
        dl->AddRectFilled(ImVec2(sx, mid - 6.0f * k), ImVec2(sx + 12.0f * k, mid + 6.0f * k), ImGui::ColorConvertFloat4ToU32(*swatch), 2.0f * k);
        dl->AddRect(ImVec2(sx, mid - 6.0f * k), ImVec2(sx + 12.0f * k, mid + 6.0f * k), color::kLine, 2.0f * k);
    }
    textAligned(dl, g_fonts.medium, 18.0f * k, ImVec2(center, mid), ImVec2(0.5f, 0.5f), color::kPaper, value, 0.0f, false);
    ImGui::PopID();
    return result;
}

void rowCaption(const char* caption, const char* hint) {
    const float k = scale();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color::kMuted));
    ImGui::TextUnformatted(caption);
    ImGui::PopStyleColor();
    if (hint && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hint);
    ImGui::SameLine(width * kCaptionShare + ImGui::GetStyle().WindowPadding.x * 0.0f);
    ImGui::SetNextItemWidth(-1.0f);
    (void)k;
}

void section(const char* title) {
    const float k = scale();
    ImGui::Dummy(ImVec2(0.0f, 10.0f * k));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    label(dl, pos, color::kBrass, title, 13.5f);
    dl->AddLine(ImVec2(pos.x, pos.y + 26.0f * k), ImVec2(pos.x + width, pos.y + 26.0f * k), color::kLine);
    ImGui::Dummy(ImVec2(width, 34.0f * k));
}

void pageHeader(const char* title, const char* description) {
    const float k = scale();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    text(dl, g_fonts.heavy, 52.0f * k, pos, color::kPaper, upper(title), 2.0f * k, false);
    float height = 60.0f * k;
    if (description && *description) {
        text(dl, g_fonts.body, 18.0f * k, ImVec2(pos.x, pos.y + 62.0f * k), color::kMuted, description, 0.0f, false);
        height += 30.0f * k;
    }
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, height));
}

void keyHint(const char* key, const char* action, float keyWidth) {
    const float k = scale();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float height = 32.0f * k;
    const ImVec2 extent = measure(g_fonts.label, 15.0f * k, upper(key), 1.5f * k);
    const float chip = std::max(extent.x + 20.0f * k, height);
    dl->AddRectFilled(pos, ImVec2(pos.x + chip, pos.y + height), IM_COL32(237, 232, 218, 20), 3.0f * k);
    dl->AddRect(pos, ImVec2(pos.x + chip, pos.y + height), IM_COL32(237, 232, 218, 70), 3.0f * k);
    dl->AddLine(ImVec2(pos.x + 3.0f * k, pos.y + height - 1.0f), ImVec2(pos.x + chip - 3.0f * k, pos.y + height - 1.0f),
                IM_COL32(237, 232, 218, 110), 2.0f);
    textAligned(dl, g_fonts.label, 15.0f * k, ImVec2(pos.x + chip * 0.5f, pos.y + height * 0.5f), ImVec2(0.5f, 0.5f), color::kPaper,
                upper(key), 1.5f * k, false);
    textAligned(dl, g_fonts.body, 18.0f * k, ImVec2(pos.x + keyWidth, pos.y + height * 0.5f), ImVec2(0.0f, 0.5f), color::kMuted, action,
                0.0f, false);
    ImGui::Dummy(ImVec2(keyWidth + measure(g_fonts.body, 18.0f * k, action).x, height + 6.0f * k));
}

bool toggle(const char* caption, bool* value) {
    const float k = scale();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 36.0f * k;
    const bool pressed = ImGui::InvisibleButton(caption, ImVec2(width, height));
    if (pressed) *value = !*value;
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetItemID();
    float t = storage->GetFloat(key, *value ? 1.0f : 0.0f);
    t += ((*value ? 1.0f : 0.0f) - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * 16.0f);
    storage->SetFloat(key, t);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hot) dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(237, 232, 218, 10));
    const float mid = pos.y + height * 0.5f;
    textAligned(dl, g_fonts.body, 18.0f * k, ImVec2(pos.x + 2.0f * k, mid), ImVec2(0.0f, 0.5f), *value ? color::kPaper : color::kMuted,
                caption, 0.0f, false);
    const float w = 42.0f * k, h = 22.0f * k;
    const ImVec2 a(pos.x + width - w - 4.0f * k, mid - h * 0.5f), b(a.x + w, a.y + h);
    dl->AddRectFilled(a, b, mix(IM_COL32(237, 232, 218, 30), color::kBrass, t), h * 0.5f);
    if (hot) dl->AddRect(a, b, withAlpha(color::kPaper, 0.4f), h * 0.5f);
    const float knob = h * 0.5f - 3.0f * k;
    dl->AddCircleFilled(ImVec2(a.x + h * 0.5f + (w - h) * t, mid), knob, mix(color::kPaper, color::kInk, t * 0.85f));
    return pressed;
}

bool button(const char* s, float width, bool primary) {
    const float k = scale();
    const std::string caption = upper(s);
    const ImVec2 extent = measure(g_fonts.display, 19.0f * k, caption, 1.2f * k);
    const ImVec2 size(width > 0.0f ? width : ImGui::GetContentRegionAvail().x, extent.y + 18.0f * k);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(s, size);
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    const bool held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (primary) {
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), held ? mix(color::kBrass, color::kInk, 0.2f)
                                                                       : hot ? mix(color::kBrass, color::kPaper, 0.2f) : color::kBrass, 2.0f * k);
    } else {
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(237, 232, 218, hot ? 30 : 14), 2.0f * k);
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), hot ? color::kBrass : IM_COL32(237, 232, 218, 50), 2.0f * k);
    }
    textAligned(dl, g_fonts.display, 19.0f * k, ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f), ImVec2(0.5f, 0.5f),
                primary ? color::kInk : (hot ? color::kPaper : mix(color::kPaper, color::kMuted, 0.3f)), caption, 1.2f * k, false);
    return pressed;
}

} // namespace origon::ui
