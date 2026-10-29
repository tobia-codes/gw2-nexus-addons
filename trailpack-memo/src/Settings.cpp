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

std::string Settings::ToJson() const
{
    auto optional = [](const OptionalColor& c) {
        return c.set ? colorJson(c.color) : nlohmann::json();
    };
    nlohmann::json j;
    j["show_qa_icon"]     = showQaIcon;
    j["show_border"]      = showBorder;
    j["edit_preview"]     = editPreview;
    j["text_scale"]       = uiTextScale;
    j["note_text_scale"]  = noteTextScale;
    j["list_width"]       = listWidth;
    j["language"]         = languageKey(language);
    j["color_border"]     = colorJson(border);
    j["color_background"] = optional(background);
    j["color_text"]       = optional(text);
    j["color_group_name"] = optional(groupName);
    j["color_no_group"]   = optional(noGroup);
    j["color_list_background"] = optional(listBackground);
    j["color_list_text"]       = optional(listText);
    j["color_note_background"] = optional(noteBackground);
    j["color_note_text"]       = optional(noteText);
    j["show_list_border"]      = showListBorder;
    j["show_note_border"]      = showNoteBorder;
    j["color_list_border"]     = optional(listBorder);
    j["color_note_border"]     = optional(noteBorder);
    j["color_daily_badge"]       = colorJson(dailyBadge);
    j["color_daily_badge_text"]  = colorJson(dailyBadgeText);
    j["color_weekly_badge"]      = colorJson(weeklyBadge);
    j["color_weekly_badge_text"] = colorJson(weeklyBadgeText);
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
    // An optional colour: a valid array sets it, null (or anything else) unsets it.
    auto optional = [&](const char* key, OptionalColor& out) {
        if (j.contains(key)) out.set = colorFrom(j[key], out.color);
    };

    boolean("show_qa_icon", showQaIcon);
    boolean("show_border", showBorder);
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
    auto color = [&](const char* key, Rgba& out) {
        if (j.contains(key)) colorFrom(j[key], out);
    };
    color("color_border", border);
    color("color_daily_badge", dailyBadge);
    color("color_daily_badge_text", dailyBadgeText);
    color("color_weekly_badge", weeklyBadge);
    color("color_weekly_badge_text", weeklyBadgeText);
    optional("color_background", background);
    optional("color_text", text);
    optional("color_group_name", groupName);
    optional("color_no_group", noGroup);
    optional("color_list_background", listBackground);
    optional("color_list_text", listText);
    optional("color_note_background", noteBackground);
    optional("color_note_text", noteText);
    boolean("show_list_border", showListBorder);
    boolean("show_note_border", showNoteBorder);
    optional("color_list_border", listBorder);
    optional("color_note_border", noteBorder);
    return true;
}

} // namespace Magpie
