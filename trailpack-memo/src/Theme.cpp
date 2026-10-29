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

void PopWindowStyle() {
    ImGui::PopStyleColor(s_PushedColors);
    ImGui::PopStyleVar(1);
    s_PushedColors = 0;
}

} // namespace Theme
