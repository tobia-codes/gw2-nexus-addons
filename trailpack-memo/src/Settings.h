#pragma once
// Settings.h — PURE addon settings (settings.json) with validation; no ImGui/Nexus.

#include <string>

namespace Magpie {

struct Rgba {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

// A colour the user may or may not have picked (unset = follow the default).
struct OptionalColor {
    bool set = false;
    Rgba color;
};

struct Settings {
    // UI language: Auto follows Nexus's language (German if Nexus is German, else English).
    enum class Language { Auto, En, De };

    static constexpr int   kScaleMin = 70;    // text sizes, percent
    static constexpr int   kScaleMax = 200;
    static constexpr float kDefaultListWidth = 200.0f;

    bool     showQaIcon    = true;
    bool     showBorder    = true;    // window border on/off
    bool     editPreview   = false;
    int      uiTextScale   = 100;   // window UI text, percent
    int      noteTextScale = 100;   // note content, percent
    float    listWidth     = kDefaultListWidth;   // px; clamped to the window when drawn
    Language language      = Language::Auto;

    Rgba          border{ 0.90f, 0.75f, 0.25f, 0.90f };   // gold
    OptionalColor background;   // unset: Nexus style
    OptionalColor text;         // unset: Nexus style
    OptionalColor groupName;    // unset: follow `border`
    OptionalColor noGroup;      // "No group" label; unset: Nexus disabled text
    Rgba          dailyBadge     { 0.90f, 0.62f, 0.20f, 1.0f };   // amber
    Rgba          dailyBadgeText { 0.08f, 0.08f, 0.08f, 1.0f };
    Rgba          weeklyBadge    { 0.40f, 0.60f, 0.95f, 1.0f };   // blue
    Rgba          weeklyBadgeText{ 0.08f, 0.08f, 0.08f, 1.0f };

    // settings.json. Colours are [r,g,b,a] (0..1); an unset optional colour is null.
    std::string ToJson() const;
    // Read what is valid, keep defaults for anything missing or of the wrong type; values
    // are clamped to their ranges. False (nothing changed) if the text isn't a JSON object.
    bool        FromJson(const std::string& json);
};

} // namespace Magpie
