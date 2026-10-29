// Theme.cpp — user-colour state + window style push/pop for Theme.h.
#include "Theme.h"

namespace Theme {

static UserColors s_Colors;
static int        s_PushedColors = 0;

UserColors& Colors() { return s_Colors; }

void PushWindowStyle() {
    s_PushedColors = 0;
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, BORDER_SIZE);
}

void PopWindowStyle() {
    ImGui::PopStyleColor(s_PushedColors);
    ImGui::PopStyleVar(1);
    s_PushedColors = 0;
}

} // namespace Theme
