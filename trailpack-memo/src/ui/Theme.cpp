// Theme.cpp — user-colour state + window style push/pop for Theme.h.
#include "ui/Theme.h"

#include <cstdarg>
#include <initializer_list>

namespace Theme {

static UserColors s_Colors;
static int        s_PushedColors = 0;
static ImVec4     s_WindowText   = { 1.0f, 1.0f, 1.0f, 1.0f };
static ImVec4     s_NexusText    = { 1.0f, 1.0f, 1.0f, 1.0f };   // before our pushes
static ImVec4     s_PopupBg      = { 0.0f, 0.0f, 0.0f, 1.0f };   // in use, set by PushWindowStyle
static ImVec4     s_PopupText    = { 1.0f, 1.0f, 1.0f, 1.0f };
static float      s_Rounding     = 0.0f;

UserColors& Colors() { return s_Colors; }
void        SetRounding(float px) { s_Rounding = px; }
float       Rounding() { return s_Rounding; }
ImVec4      WindowText() { return s_WindowText; }
ImVec4      PopupText()  { return s_PopupText; }

ImVec4 DefaultPopupBg() {
    const ImGuiStyle& st = ImGui::GetStyle();
    const ImVec4 list = s_Colors.hasListBg ? s_Colors.listBg : st.Colors[ImGuiCol_ChildBg];
    const ImVec4 win  = s_Colors.hasBackground ? s_Colors.background : st.Colors[ImGuiCol_WindowBg];
    const float  a    = list.w;
    return ImVec4(list.x * a + win.x * (1.0f - a), list.y * a + win.y * (1.0f - a),
                  list.z * a + win.z * (1.0f - a), 1.0f);
}

ImVec4 DefaultPopupText() {
    return s_Colors.hasListText ? s_Colors.listText : ImGui::GetStyle().Colors[ImGuiCol_Text];
}

PopupTextScope::PopupTextScope() {
    ImGui::PushStyleColor(ImGuiCol_Text, s_PopupText);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, FieldBg(s_PopupBg));
}

void Tooltip(const char* fmt, ...) {
    PopupTextScope text;
    va_list args;
    va_start(args, fmt);
    ImGui::SetTooltipV(fmt, args);
    va_end(args);
}

void PushWindowStyle() {
    s_PushedColors = 0;
    s_NexusText  = ImGui::GetStyle().Colors[ImGuiCol_Text];
    s_WindowText = s_Colors.hasText ? s_Colors.text : s_NexusText;
    // Popup colours are resolved here, before this window's own colours are pushed.
    s_PopupBg   = s_Colors.hasPopupBg   ? s_Colors.popupBg   : DefaultPopupBg();
    s_PopupText = s_Colors.hasPopupText ? s_Colors.popupText : DefaultPopupText();
    ImGui::PushStyleColor(ImGuiCol_PopupBg, s_PopupBg);
    ++s_PushedColors;
    // Nexus's buttons are translucent: make them opaque, blended over the pane colour they
    // mostly sit on, so they look the same but nothing shows through.
    {
        const ImGuiStyle& st = ImGui::GetStyle();
        const ImVec4 base = DefaultPopupBg();
        for (ImGuiCol idx : { ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive }) {
            const ImVec4 c = st.Colors[idx];
            ImGui::PushStyleColor(idx, ImVec4(c.x * c.w + base.x * (1.0f - c.w), c.y * c.w + base.y * (1.0f - c.w),
                                              c.z * c.w + base.z * (1.0f - c.w), 1.0f));
            ++s_PushedColors;
        }
    }
    ImGui::PushStyleColor(ImGuiCol_Border, s_Colors.border);
    ++s_PushedColors;
    if (s_Colors.hasBackground) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, s_Colors.background);
        ++s_PushedColors;
    }
    if (s_Colors.hasText) {
        ImGui::PushStyleColor(ImGuiCol_Text, s_Colors.text);
        ++s_PushedColors;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, s_Colors.showBorder ? BORDER_SIZE : 0.0f);
    PushRounding();
}

static constexpr ImGuiStyleVar kRoundingVars[] = {
    ImGuiStyleVar_WindowRounding, ImGuiStyleVar_ChildRounding, ImGuiStyleVar_FrameRounding,
    ImGuiStyleVar_PopupRounding, ImGuiStyleVar_GrabRounding, ImGuiStyleVar_ScrollbarRounding,
    ImGuiStyleVar_TabRounding,
};

float TitleBarPadY() {
    return ImGui::GetStyle().FramePadding.y + 4.0f;
}

bool BeginWindow(const char* name, bool* open) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, TitleBarPadY()));
    const bool shown = ImGui::Begin(name, open);
    ImGui::PopStyleVar();
    return shown;
}

void PushRounding() {
    for (ImGuiStyleVar v : kRoundingVars) ImGui::PushStyleVar(v, s_Rounding);
}

void PopRounding() {
    ImGui::PopStyleVar((int)(sizeof kRoundingVars / sizeof kRoundingVars[0]));
}

void PushPaneBorder(bool show, bool hasColor, const ImVec4& color) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, show ? 1.0f : 0.0f);
    const float f = ImGui::GetFontSize();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(f * 0.5f, f * 0.4f));
    ImGui::PushStyleColor(ImGuiCol_Border, hasColor ? color : s_Colors.border);
}

ImVec4 FieldBg(const ImVec4& bg) {
    const float lum = 0.299f * bg.x + 0.587f * bg.y + 0.114f * bg.z;
    auto shift = [&](float v) {
        v = lum > 0.5f ? v * 0.93f : v + 0.07f;
        return v > 1.0f ? 1.0f : v;
    };
    return ImVec4(shift(bg.x), shift(bg.y), shift(bg.z), 1.0f);
}

void PopPaneBorder() {
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(2);
}

void PopWindowStyle() {
    ImGui::PopStyleColor(s_PushedColors);
    PopRounding();
    ImGui::PopStyleVar(1);
    s_PushedColors = 0;
}

} // namespace Theme
