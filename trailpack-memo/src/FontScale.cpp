// FontScale.cpp — see FontScale.h.
#include "FontScale.h"

namespace Magpie {

static ImFont* s_Normal = nullptr;
static ImFont* s_Big    = nullptr;
static float   s_BasePx = 0.0f;

void SetFontBase(ImFont* normal, ImFont* big)
{
    s_Normal = normal;
    s_Big    = big;
    s_BasePx = ImGui::GetFontSize();
}

float BaseFontPx() { return s_BasePx; }

float HeadingRatio()
{
    if (s_Normal == nullptr || s_Big == nullptr || s_Normal->FontSize <= 0.0f) return 1.0f;
    return s_Big->FontSize / s_Normal->FontSize;
}

FontScaleScope::FontScaleScope(float scale)
{
    ImFont* current = ImGui::GetFont();
    const float target = (s_BasePx > 0.0f ? s_BasePx : ImGui::GetFontSize()) * scale;

    // Use FontBig as soon as the normal font would have to be stretched: its bitmap is
    // larger, so it is scaled down (or stretched less) to reach the target size.
    ImFont* normal = s_Normal != nullptr ? s_Normal : current;
    ImFont* chosen = normal;
    if (s_Big != nullptr && s_Big->FontSize > normal->FontSize &&
        target > normal->FontSize * 1.05f)
        chosen = s_Big;

    // Window font-scale factor in effect here (1 unless SetWindowFontScale was used):
    // GetFontSize() = FontGlobalScale * font->FontSize * font->Scale * windowFactor.
    const float globalScale = ImGui::GetIO().FontGlobalScale;
    const float natural     = globalScale * current->FontSize * current->Scale;
    const float windowScale = natural > 0.0f ? ImGui::GetFontSize() / natural : 1.0f;

    font_     = chosen;
    oldScale_ = chosen->Scale;
    const float denom = globalScale * chosen->FontSize * windowScale;
    if (denom > 0.0f) chosen->Scale = target / denom;
    ImGui::PushFont(chosen);
}

FontScaleScope::~FontScaleScope()
{
    font_->Scale = oldScale_;
    ImGui::PopFont();
}

} // namespace Magpie
