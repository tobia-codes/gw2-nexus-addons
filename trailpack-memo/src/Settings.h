#pragma once
// Settings.h — PURE addon settings (settings.json) with validation; no ImGui/Nexus.

#include <string>
#include <vector>

namespace Magpie {

struct Rgba {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    bool operator==(const Rgba& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

// A colour the user may or may not have picked (unset = follow the default).
struct OptionalColor {
    bool set = false;
    Rgba color;
    // Unset colours are equal whatever their stored value.
    bool operator==(const OptionalColor& o) const { return set == o.set && (!set || color == o.color); }
};

// Everything under "Colour options" (incl. the border toggles): what a theme stores.
struct ColorSet {
    bool          showBorder = false;   // window border on/off
    Rgba          border{ 0.90f, 0.75f, 0.25f, 0.90f };   // gold
    OptionalColor background;   // unset: Nexus style
    OptionalColor text;         // unset: Nexus style
    OptionalColor groupName;    // unset: follow `border`
    OptionalColor noGroup;      // "No group" label; unset: Nexus disabled text
    OptionalColor listBackground;   // note list pane; unset: Nexus child background
    OptionalColor listText;         // note titles in the list; unset: window text
    OptionalColor noteBackground;   // note pane; unset: Nexus child background
    OptionalColor noteText;         // note title, body, editor; unset: window text
    bool          showListBorder = false;   // pane borders on/off
    bool          showNoteBorder = false;
    OptionalColor listBorder;       // unset: follow `border`
    OptionalColor noteBorder;       // unset: follow `border`
    OptionalColor menuButton;       // menu button next to "Create New Note"; unset: Nexus button
    OptionalColor menuIcon;         // its three lines; unset: window text
    Rgba          popupBackground{ 0.0f, 0.0f, 0.0f, 1.0f };   // menus, tooltips, modals: opaque black
    OptionalColor popupText;        // unset: Nexus text
    Rgba          dailyBadge     { 0.90f, 0.62f, 0.20f, 1.0f };   // amber
    Rgba          dailyBadgeText { 0.08f, 0.08f, 0.08f, 1.0f };
    Rgba          weeklyBadge    { 0.40f, 0.60f, 0.95f, 1.0f };   // blue
    Rgba          weeklyBadgeText{ 0.08f, 0.08f, 0.08f, 1.0f };

    bool operator==(const ColorSet& o) const;
    bool operator!=(const ColorSet& o) const { return !(*this == o); }

    // Built-in themes. Default = all of the above (Nexus look).
    static ColorSet Light();
    static ColorSet Dark();
};

// A colour theme the user saved under a name.
struct NamedTheme {
    std::string name;
    ColorSet    colors;
};

struct Settings {
    // UI language: Auto follows Nexus's language (German if Nexus is German, else English).
    enum class Language { Auto, En, De };

    static constexpr int   kScaleMin = 70;    // text sizes, percent
    static constexpr int   kScaleMax = 200;
    static constexpr float kDefaultListWidth = 200.0f;

    bool     showQaIcon    = true;
    bool     editPreview   = false;
    int      uiTextScale   = 100;   // window UI text, percent
    int      noteTextScale = 100;   // note content, percent
    float    listWidth     = kDefaultListWidth;   // px; clamped to the window when drawn
    Language language      = Language::Auto;

    ColorSet                colors;
    std::vector<NamedTheme> themes;   // user themes, in the order saved

    // Save `colors` as a user theme: same name (trimmed, case-sensitive) is replaced,
    // otherwise appended. False for an empty name.
    bool SaveTheme(const std::string& name, const ColorSet& colors);
    bool DeleteTheme(const std::string& name);
    const NamedTheme* FindTheme(const std::string& name) const;

    // settings.json. Colours are flat keys ("color_border": [r,g,b,a] in 0..1, an unset
    // optional colour is null); user themes are "themes": [{"name": ..., <colour keys>}].
    std::string ToJson() const;
    // Read what is valid, keep defaults for anything missing or of the wrong type; values
    // are clamped to their ranges. False (nothing changed) if the text isn't a JSON object.
    bool        FromJson(const std::string& json);
};

} // namespace Magpie
