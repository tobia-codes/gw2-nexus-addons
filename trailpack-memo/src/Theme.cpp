// Theme.cpp — user-colour state + window style push/pop for Theme.h.
#include "Theme.h"

namespace Theme {

static UserColors s_Colors;
static int        s_PushedColors = 0;
static ImVec4     s_WindowText   = { 1.0f, 1.0f, 1.0f, 1.0f };

UserColors& Colors() { return s_Colors; }
ImVec4      WindowText() { return s_WindowText; }

void PushWindowStyle() {
    s_PushedColors = 0;
    s_WindowText = s_Colors.hasText ? s_Colors.text : ImGui::GetStyle().Colors[ImGuiCol_Text];
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
}

void PushPaneBorder(bool show, bool hasColor, const ImVec4& color) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, show ? 1.0f : 0.0f);
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
    ImGui::PopStyleVar(1);
}

void PopWindowStyle() {
    ImGui::PopStyleColor(s_PushedColors);
    ImGui::PopStyleVar(1);
    s_PushedColors = 0;
}

} // namespace Theme
