// Settings.cpp — see Settings.h.
#include "Settings.h"

#include <nlohmann/json.hpp>

namespace Magpie {

static nlohmann::json colorJson(const Rgba& c) { return { c.r, c.g, c.b, c.a }; }

static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

static bool colorFrom(const nlohmann::json& j, Rgba& out)
{
    if (!j.is_array() || j.size() != 4) return false;
    for (int i = 0; i < 4; ++i) if (!j[i].is_number()) return false;
    out = Rgba{ clamp01(j[0].get<float>()), clamp01(j[1].get<float>()),
                clamp01(j[2].get<float>()), clamp01(j[3].get<float>()) };
    return true;
}

static const char* languageKey(Settings::Language l)
{
    return l == Settings::Language::En ? "en" : l == Settings::Language::De ? "de" : "auto";
}

static std::string trimmed(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ── ColorSet ──────────────────────────────────────────────────────────────

bool ColorSet::operator==(const ColorSet& o) const
{
    return showBorder == o.showBorder && border == o.border &&
           background == o.background && text == o.text &&
           groupName == o.groupName && noGroup == o.noGroup &&
           listBackground == o.listBackground && listText == o.listText &&
           noteBackground == o.noteBackground && noteText == o.noteText &&
           showListBorder == o.showListBorder && showNoteBorder == o.showNoteBorder &&
           listBorder == o.listBorder && noteBorder == o.noteBorder &&
           menuButton == o.menuButton && menuIcon == o.menuIcon &&
           dailyBadge == o.dailyBadge && dailyBadgeText == o.dailyBadgeText &&
           weeklyBadge == o.weeklyBadge && weeklyBadgeText == o.weeklyBadgeText;
}

// Light panes with dark text; the window keeps Nexus's text colour, because buttons,
// popups and the title bar stay Nexus-dark.
ColorSet ColorSet::Light()
{
    ColorSet c;
    c.border         = Rgba{ 0.55f, 0.56f, 0.60f, 1.0f };
    c.background     = { true, Rgba{ 0.72f, 0.73f, 0.77f, 0.97f } };
    c.groupName      = { true, Rgba{ 0.48f, 0.30f, 0.00f, 1.0f } };
    c.noGroup        = { true, Rgba{ 0.28f, 0.29f, 0.33f, 1.0f } };
    c.listBackground = { true, Rgba{ 0.93f, 0.93f, 0.95f, 1.0f } };
    c.listText       = { true, Rgba{ 0.05f, 0.05f, 0.07f, 1.0f } };
    c.noteBackground = { true, Rgba{ 0.98f, 0.98f, 0.98f, 1.0f } };
    c.noteText       = { true, Rgba{ 0.05f, 0.05f, 0.07f, 1.0f } };
    c.menuButton     = { true, Rgba{ 0.58f, 0.59f, 0.64f, 1.0f } };
    c.menuIcon       = { true, Rgba{ 0.05f, 0.05f, 0.07f, 1.0f } };
    return c;
}

ColorSet ColorSet::Dark()
{
    ColorSet c;
    c.background     = { true, Rgba{ 0.06f, 0.06f, 0.08f, 0.97f } };
    c.noGroup        = { true, Rgba{ 0.55f, 0.55f, 0.60f, 1.0f } };
    c.listBackground = { true, Rgba{ 0.10f, 0.10f, 0.12f, 1.0f } };
    c.listText       = { true, Rgba{ 0.88f, 0.88f, 0.90f, 1.0f } };
    c.noteBackground = { true, Rgba{ 0.08f, 0.08f, 0.10f, 1.0f } };
    c.noteText       = { true, Rgba{ 0.90f, 0.90f, 0.92f, 1.0f } };
    return c;
}

static void colorsToJson(const ColorSet& c, nlohmann::json& j)
{
    auto optional = [](const OptionalColor& o) {
        return o.set ? colorJson(o.color) : nlohmann::json();
    };
    j["show_border"]             = c.showBorder;
    j["color_border"]            = colorJson(c.border);
    j["color_background"]        = optional(c.background);
    j["color_text"]              = optional(c.text);
    j["color_group_name"]        = optional(c.groupName);
    j["color_no_group"]          = optional(c.noGroup);
    j["color_list_background"]   = optional(c.listBackground);
    j["color_list_text"]         = optional(c.listText);
    j["color_note_background"]   = optional(c.noteBackground);
    j["color_note_text"]         = optional(c.noteText);
    j["show_list_border"]        = c.showListBorder;
    j["show_note_border"]        = c.showNoteBorder;
    j["color_list_border"]       = optional(c.listBorder);
    j["color_note_border"]       = optional(c.noteBorder);
    j["color_menu_button"]       = optional(c.menuButton);
    j["color_menu_icon"]         = optional(c.menuIcon);
    j["color_daily_badge"]       = colorJson(c.dailyBadge);
    j["color_daily_badge_text"]  = colorJson(c.dailyBadgeText);
    j["color_weekly_badge"]      = colorJson(c.weeklyBadge);
    j["color_weekly_badge_text"] = colorJson(c.weeklyBadgeText);
}

static void colorsFromJson(const nlohmann::json& j, ColorSet& c)
{
    auto boolean = [&](const char* key, bool& out) {
        if (j.contains(key) && j[key].is_boolean()) out = j[key].get<bool>();
    };
    auto color = [&](const char* key, Rgba& out) {
        if (j.contains(key)) colorFrom(j[key], out);
    };
    // An optional colour: a valid array sets it, null (or anything else) unsets it.
    auto optional = [&](const char* key, OptionalColor& out) {
        if (j.contains(key)) out.set = colorFrom(j[key], out.color);
    };
    boolean("show_border", c.showBorder);
    color("color_border", c.border);
    optional("color_background", c.background);
    optional("color_text", c.text);
    optional("color_group_name", c.groupName);
    optional("color_no_group", c.noGroup);
    optional("color_list_background", c.listBackground);
    optional("color_list_text", c.listText);
    optional("color_note_background", c.noteBackground);
    optional("color_note_text", c.noteText);
    boolean("show_list_border", c.showListBorder);
    boolean("show_note_border", c.showNoteBorder);
    optional("color_list_border", c.listBorder);
    optional("color_note_border", c.noteBorder);
    optional("color_menu_button", c.menuButton);
    optional("color_menu_icon", c.menuIcon);
    color("color_daily_badge", c.dailyBadge);
    color("color_daily_badge_text", c.dailyBadgeText);
    color("color_weekly_badge", c.weeklyBadge);
    color("color_weekly_badge_text", c.weeklyBadgeText);
}

// ── Themes ────────────────────────────────────────────────────────────────

bool Settings::SaveTheme(const std::string& name, const ColorSet& set)
{
    const std::string n = trimmed(name);
    if (n.empty()) return false;
    for (auto& t : themes) {
        if (t.name == n) { t.colors = set; return true; }
    }
    themes.push_back(NamedTheme{ n, set });
    return true;
}

bool Settings::DeleteTheme(const std::string& name)
{
    for (auto it = themes.begin(); it != themes.end(); ++it) {
        if (it->name == name) { themes.erase(it); return true; }
    }
    return false;
}

const NamedTheme* Settings::FindTheme(const std::string& name) const
{
    for (const auto& t : themes) if (t.name == name) return &t;
    return nullptr;
}

// ── settings.json ─────────────────────────────────────────────────────────

std::string Settings::ToJson() const
{
    nlohmann::json j;
    j["show_qa_icon"]     = showQaIcon;
    j["edit_preview"]     = editPreview;
    j["text_scale"]       = uiTextScale;
    j["note_text_scale"]  = noteTextScale;
    j["list_width"]       = listWidth;
    j["language"]         = languageKey(language);
    colorsToJson(colors, j);
    nlohmann::json list = nlohmann::json::array();
    for (const auto& t : themes) {
        nlohmann::json tj;
        tj["name"] = t.name;
        colorsToJson(t.colors, tj);
        list.push_back(tj);
    }
    j["themes"] = list;
    return j.dump(2);
}

bool Settings::FromJson(const std::string& json)
{
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json);
    } catch (...) {
        return false;
    }
    if (!j.is_object()) return false;

    auto boolean = [&](const char* key, bool& out) {
        if (j.contains(key) && j[key].is_boolean()) out = j[key].get<bool>();
    };
    auto scale = [&](const char* key, int& out) {
        if (!j.contains(key) || !j[key].is_number_integer()) return;
        const int v = j[key].get<int>();
        out = v < kScaleMin ? kScaleMin : v > kScaleMax ? kScaleMax : v;
    };

    boolean("show_qa_icon", showQaIcon);
    boolean("edit_preview", editPreview);
    scale("text_scale", uiTextScale);
    scale("note_text_scale", noteTextScale);
    if (j.contains("list_width") && j["list_width"].is_number()) {
        const float w = j["list_width"].get<float>();
        if (w > 0.0f) listWidth = w;
    }
    if (j.contains("language") && j["language"].is_string()) {
        const std::string l = j["language"].get<std::string>();
        language = l == "en" ? Language::En : l == "de" ? Language::De : Language::Auto;
    }
    colorsFromJson(j, colors);

    // Themes: entries without a usable name are skipped; missing keys keep defaults.
    if (j.contains("themes") && j["themes"].is_array()) {
        themes.clear();
        for (const auto& tj : j["themes"]) {
            if (!tj.is_object() || !tj.contains("name") || !tj["name"].is_string()) continue;
            ColorSet set;
            colorsFromJson(tj, set);
            SaveTheme(tj["name"].get<std::string>(), set);
        }
    }
    return true;
}

} // namespace Magpie
