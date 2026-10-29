// Theme.cpp — user-colour state + window style push/pop for Theme.h.
#include "Theme.h"

#include <cstdarg>

namespace Theme {

static UserColors s_Colors;
static int        s_PushedColors = 0;
static ImVec4     s_WindowText   = { 1.0f, 1.0f, 1.0f, 1.0f };
static ImVec4     s_NexusText    = { 1.0f, 1.0f, 1.0f, 1.0f };   // before our pushes

UserColors& Colors() { return s_Colors; }
ImVec4      WindowText() { return s_WindowText; }
ImVec4      PopupText()  { return s_Colors.hasPopupText ? s_Colors.popupText : s_NexusText; }

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
    ImGui::PushStyleColor(ImGuiCol_PopupBg, s_Colors.popupBg);
    ++s_PushedColors;
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
    ImGui::PopStyleVar(1);
    s_PushedColors = 0;
}

} // namespace Theme
