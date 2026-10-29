#pragma once
// FontScale.h — sharper scaled text (ImGui + Nexus fonts).
//
// ImGui fonts are bitmaps rasterized once at a fixed pixel size, so scaling text up
// stretches the bitmap and blurs it. FontScaleScope therefore draws large text from
// Nexus's bigger FontBig (scaled DOWN, which stays crisp) instead of stretching the
// normal font, and sets the font size directly -- independent of window font scales,
// which ImGui 1.80 only inherits one child level deep.

#include "imgui.h"

namespace Magpie {

// Once per frame, OUTSIDE any window: remember the normal font, its natural pixel size
// (ImGui::GetFontSize() at that point) and Nexus's FontBig (may be null).
void SetFontBase(ImFont* normal, ImFont* big);

// Natural pixel size of the normal font (0 until SetFontBase ran).
float BaseFontPx();
// FontBig's size relative to the normal font (heading size factor; 1 if unknown).
float HeadingRatio();

// For its lifetime, ImGui's current font size is BaseFontPx() * scale. Picks the normal
// font or FontBig, whichever needs the least upscaling, by temporarily setting that
// font's Scale (restored before PopFont, so nothing outside the scope sees it). Scopes
// nest safely.
class FontScaleScope {
public:
    explicit FontScaleScope(float scale);
    ~FontScaleScope();
    FontScaleScope(const FontScaleScope&) = delete;
    FontScaleScope& operator=(const FontScaleScope&) = delete;

private:
    ImFont* font_     = nullptr;
    float   oldScale_ = 1.0f;
};

} // namespace Magpie
