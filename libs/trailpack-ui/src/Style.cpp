#include "tpui/Style.h"

#include "imgui_internal.h"   // PushItemFlag / ImGuiItemFlags_Disabled (no BeginDisabled in 1.80)

#include <initializer_list>

namespace Trailpack { namespace Ui {

namespace {

float s_Rounding = 0.0f;

constexpr ImGuiStyleVar kRoundingVars[] = {
    ImGuiStyleVar_WindowRounding, ImGuiStyleVar_ChildRounding, ImGuiStyleVar_FrameRounding,
    ImGuiStyleVar_PopupRounding, ImGuiStyleVar_GrabRounding, ImGuiStyleVar_ScrollbarRounding,
    ImGuiStyleVar_TabRounding,
};

} // namespace

void  SetRounding(float px) { s_Rounding = px; }
float Rounding()            { return s_Rounding; }

void PushRounding() {
    for (ImGuiStyleVar v : kRoundingVars) ImGui::PushStyleVar(v, s_Rounding);
}

void PopRounding() {
    ImGui::PopStyleVar((int)(sizeof kRoundingVars / sizeof kRoundingVars[0]));
}

float TitleBarPadY() {
    return ImGui::GetStyle().FramePadding.y + 4.0f;
}

bool BeginWindow(const char* name, bool* open) {
    // The title bar reads FramePadding.y inside Begin, so it is raised only for that call.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, TitleBarPadY()));
    const bool shown = ImGui::Begin(name, open);
    ImGui::PopStyleVar();
    return shown;
}

PlainWindowScope::PlainWindowScope() {
    for (ImGuiCol idx : { ImGuiCol_WindowBg, ImGuiCol_PopupBg }) {
        ImVec4 c = ImGui::GetStyle().Colors[idx];
        c.w      = 1.0f;
        ImGui::PushStyleColor(idx, c);
    }
    PushRounding();
}

PlainWindowScope::~PlainWindowScope() {
    PopRounding();
    ImGui::PopStyleColor(2);
}

void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}

LockedScope::LockedScope(bool locked) : locked_(locked) {
    if (!locked_) return;
    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
}

LockedScope::~LockedScope() {
    if (!locked_) return;
    ImGui::PopStyleVar();
    ImGui::PopItemFlag();
}

void LockedHint(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
}

}} // namespace Trailpack::Ui
