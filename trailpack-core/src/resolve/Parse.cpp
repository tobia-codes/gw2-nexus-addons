// Parse.cpp — see Parse.h.
#include "resolve/Parse.h"

#include <nlohmann/json.hpp>

#include "chatlinks/ChatLinks.h"
#include "resolve/Labels.h"
#include "resolve/Text.h"

namespace Trailpack { namespace Core { namespace Parse {

using nlohmann::json;
using ChatLinks::LinkType;

namespace {

// Parse without exceptions; a non-array body is a failed response.
bool ParseArray(const std::string& body, json& out)
{
    out = json::parse(body, nullptr, false);
    return out.is_array();
}

std::string Str(const json& o, const char* key)
{
    auto it = o.find(key);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}

long long Num(const json& o, const char* key, long long def = 0)
{
    auto it = o.find(key);
    if (it == o.end() || !it->is_number()) return def;
    return it->is_number_float() ? static_cast<long long>(it->get<double>()) : it->get<long long>();
}

bool Has(const json& o, const char* key)
{
    auto it = o.find(key);
    return it != o.end() && !it->is_null();
}

bool HasFlag(const json& o, const char* flag)
{
    auto it = o.find("flags");
    if (it == o.end() || !it->is_array()) return false;
    for (const auto& f : *it)
        if (f.is_string() && f.get<std::string>() == flag) return true;
    return false;
}

// Common head of every batch object; false if it has no usable id / name.
bool Head(const json& o, uint8_t type, Lang lang, Record& r)
{
    if (!o.is_object()) return false;
    const long long id = Num(o, "id", -1);
    r = Record{};
    r.type = type;
    r.id   = id > 0 ? static_cast<uint32_t>(id) : 0;
    r.lang = lang;
    r.name = Text::Trim(Str(o, "name"));
    r.icon = Str(o, "icon");
    r.description = Text::StripGameMarkup(Str(o, "description"));
    return r.id != 0 && !r.name.empty();
}

Rarity ParseRarity(const std::string& s)
{
    static const char* const kNames[] = {"Junk", "Basic", "Fine", "Masterwork", "Rare",
                                         "Exotic", "Ascended", "Legendary"};
    for (int i = 0; i < 8; ++i)
        if (s == kNames[i]) return static_cast<Rarity>(i + 1);
    return Rarity::Unknown;
}

std::string N(long long v) { return std::to_string(v); }

// One API skill / trait fact to a tooltip line ("" = skip).
std::string FormatFact(const json& f, Lang lang)
{
    const std::string type = Str(f, "type");
    const std::string text = Str(f, "text");
    auto labeled = [&](const std::string& value) { return text.empty() ? value : text + ": " + value; };

    if (type == "Recharge")              return labeled(N(Num(f, "value")) + "s");
    if (type == "Range" || type == "Number") return labeled(N(Num(f, "value")));
    if (type == "Distance")              return labeled(N(Has(f, "distance") ? Num(f, "distance") : Num(f, "value")));
    if (type == "Percent")               return labeled(N(Has(f, "percent") ? Num(f, "percent") : Num(f, "value")) + "%");
    if (type == "Duration" || type == "Time") return labeled(N(Num(f, "duration")) + "s");
    if (type == "AttributeAdjust")       return "+" + N(Num(f, "value")) + " " + Labels::Attribute(Str(f, "target"), lang);
    if (type == "Buff" || type == "PrefixedBuff") {
        const std::string status = Str(f, "status");
        if (status.empty()) return text;
        const long long count = Num(f, "apply_count", 1);
        const long long dur   = Num(f, "duration");
        std::string line = (count > 1 ? N(count) + " " : "") + status;
        if (dur > 0) line += " (" + N(dur) + "s)";
        const std::string desc = Text::StripGameMarkup(Str(f, "description"));
        return desc.empty() ? line : line + ": " + desc;
    }
    if (type == "ComboField")
        return (text.empty() ? std::string(Labels::Tr("Combo Field", lang)) : text) + ": " + Str(f, "field_type");
    if (type == "ComboFinisher") {
        std::string line = (text.empty() ? std::string(Labels::Tr("Combo Finisher", lang)) : text) + ": " + Str(f, "finisher_type");
        const long long pct = Num(f, "percent", 100);
        return pct > 0 && pct < 100 ? line + " (" + N(pct) + "%)" : line;
    }
    if (type == "BuffConversion")        return labeled(N(Num(f, "percent")) + "%");
    if (type == "Damage" || type == "Heal" || type == "HealingAdjust") {
        const long long hits = Num(f, "hit_count", 1);
        return hits > 1 ? text + " (" + N(hits) + "x)" : text;
    }
    if (type == "StunBreak")             return text.empty() ? Labels::Tr("Breaks Stun", lang) : text;
    if (Has(f, "value") && f["value"].is_number()) return labeled(N(Num(f, "value")));
    return text;
}

bool SkillLike(const std::string& body, uint8_t type, Lang lang, std::vector<Record>& out)
{
    json arr;
    if (!ParseArray(body, arr)) return false;
    for (const auto& o : arr) {
        Record r;
        if (!Head(o, type, lang, r)) continue;
        auto facts = o.find("facts");
        if (facts != o.end() && facts->is_array()) {
            for (const auto& f : *facts) {
                if (!f.is_object()) continue;
                Fact fact{Str(f, "icon"), Text::Trim(FormatFact(f, lang))};
                if (!fact.text.empty()) r.facts.push_back(std::move(fact));
            }
        }
        out.push_back(std::move(r));
    }
    return true;
}

void ItemLines(const json& o, Lang lang, Record& r)
{
    auto add = [&](const std::string& line) { if (!line.empty()) r.facts.push_back({"", line}); };
    const json& d = o.contains("details") && o["details"].is_object() ? o["details"] : json::object();

    if (Num(d, "max_power") > 0)
        add(std::string(Labels::Tr("Weapon Strength", lang)) + ": " + N(Num(d, "min_power")) + " - " + N(Num(d, "max_power")));
    else if (Num(d, "defense") > 0)
        add(std::string(Labels::Tr("Defense", lang)) + ": " + N(Num(d, "defense")));

    if (d.contains("infix_upgrade") && d["infix_upgrade"].is_object()) {
        const json& infix = d["infix_upgrade"];
        if (infix.contains("attributes") && infix["attributes"].is_array())
            for (const auto& a : infix["attributes"])
                add("+" + N(Num(a, "modifier")) + " " + Labels::Attribute(Str(a, "attribute"), lang));
        if (infix.contains("buff") && infix["buff"].is_object())
            add(Text::StripGameMarkup(Str(infix["buff"], "description")));
    }
    if (d.contains("bonuses") && d["bonuses"].is_array()) {        // rune set bonuses
        int n = 0;
        for (const auto& b : d["bonuses"])
            if (b.is_string()) add("(" + N(++n) + "): " + Text::StripGameMarkup(b.get<std::string>()));
    }
    if (d.contains("infusion_slots") && d["infusion_slots"].is_array())
        for (size_t i = 0; i < d["infusion_slots"].size(); ++i) add(Labels::Tr("Unused Infusion Slot", lang));

    const std::string type = Str(o, "type");
    if (type == "Armor" || type == "Weapon" || type == "Trinket" || type == "Back") {
        add(Labels::ItemSubtype(Str(d, "type"), lang));
        add(Labels::WeightClass(Str(d, "weight_class"), lang));
    }
    if (Num(o, "level") > 0) add(std::string(Labels::Tr("Required Level", lang)) + ": " + N(Num(o, "level")));
}

} // namespace

bool Items(const std::string& body, Lang lang, std::vector<Record>& out)
{
    json arr;
    if (!ParseArray(body, arr)) return false;
    for (const auto& o : arr) {
        Record r;
        if (!Head(o, ChatLinks::LINK_ITEM, lang, r)) continue;
        r.rarity      = ParseRarity(Str(o, "rarity"));
        r.vendorValue = static_cast<int32_t>(Num(o, "vendor_value"));
        r.noSell      = HasFlag(o, "NoSell");
        // First match wins, like the in-game tooltip.
        if (HasFlag(o, "AccountBound"))            r.bound = Bound::AccountOnAcquire;
        else if (HasFlag(o, "SoulbindOnAcquire"))  r.bound = Bound::SoulOnAcquire;
        else if (HasFlag(o, "AccountBindOnUse"))   r.bound = Bound::AccountOnUse;
        else if (HasFlag(o, "SoulBindOnUse"))      r.bound = Bound::SoulOnUse;
        // NoSell only means "no vendor": Mystic Coins are NoSell and traded on the TP.
        r.tradeable = r.bound != Bound::AccountOnAcquire && r.bound != Bound::SoulOnAcquire;
        ItemLines(o, lang, r);
        out.push_back(std::move(r));
    }
    return true;
}

bool Skins(const std::string& body, Lang lang, std::vector<Record>& out)
{
    json arr;
    if (!ParseArray(body, arr)) return false;
    for (const auto& o : arr) {
        Record r;
        if (!Head(o, ChatLinks::LINK_SKIN, lang, r)) continue;
        r.rarity = ParseRarity(Str(o, "rarity"));
        if (o.contains("details") && o["details"].is_object()) {
            const std::string sub = Labels::ItemSubtype(Str(o["details"], "type"), lang);
            if (!sub.empty()) r.facts.push_back({"", sub});
            const std::string w = Labels::WeightClass(Str(o["details"], "weight_class"), lang);
            if (!w.empty()) r.facts.push_back({"", w});
        }
        out.push_back(std::move(r));
    }
    return true;
}

bool Skills(const std::string& body, Lang lang, std::vector<Record>& out)
{
    return SkillLike(body, ChatLinks::LINK_SKILL, lang, out);
}

bool Traits(const std::string& body, Lang lang, std::vector<Record>& out)
{
    return SkillLike(body, ChatLinks::LINK_TRAIT, lang, out);
}

bool Recipes(const std::string& body, std::vector<Recipe>& out)
{
    json arr;
    if (!ParseArray(body, arr)) return false;
    for (const auto& o : arr) {
        if (!o.is_object() || Num(o, "id") <= 0) continue;
        Recipe r;
        r.id           = static_cast<uint32_t>(Num(o, "id"));
        r.outputItemId = static_cast<uint32_t>(Num(o, "output_item_id"));
        r.outputCount  = static_cast<int>(Num(o, "output_item_count", 1));
        r.minRating    = static_cast<int>(Num(o, "min_rating"));
        if (o.contains("ingredients") && o["ingredients"].is_array()) {
            for (const auto& i : o["ingredients"]) {
                Ingredient in;
                in.count = static_cast<int>(Num(i, "count"));
                const std::string kind = Str(i, "type");          // newer schema: type + id
                if (kind == "GuildUpgrade") in.guildUpgradeId = static_cast<uint32_t>(Num(i, "id"));
                else in.itemId = static_cast<uint32_t>(Has(i, "item_id") ? Num(i, "item_id") : Num(i, "id"));
                if (in.itemId || in.guildUpgradeId) r.ingredients.push_back(in);
            }
        }
        if (o.contains("guild_ingredients") && o["guild_ingredients"].is_array()) {
            for (const auto& g : o["guild_ingredients"]) {
                Ingredient in;
                in.guildUpgradeId = static_cast<uint32_t>(Num(g, "upgrade_id"));
                in.count          = static_cast<int>(Num(g, "count"));
                if (in.guildUpgradeId) r.ingredients.push_back(in);
            }
        }
        out.push_back(std::move(r));
    }
    return true;
}

std::vector<uint32_t> RecipeItemIds(const Recipe& r)
{
    std::vector<uint32_t> ids;
    if (r.outputItemId) ids.push_back(r.outputItemId);
    for (const auto& i : r.ingredients)
        if (i.itemId) ids.push_back(i.itemId);
    return ids;
}

std::vector<uint32_t> RecipeGuildIds(const Recipe& r)
{
    std::vector<uint32_t> ids;
    for (const auto& i : r.ingredients)
        if (i.guildUpgradeId) ids.push_back(i.guildUpgradeId);
    return ids;
}

bool Names(const std::string& body, std::map<uint32_t, std::string>& out)
{
    json arr;
    if (!ParseArray(body, arr)) return false;
    for (const auto& o : arr)
        if (o.is_object() && Num(o, "id") > 0 && !Str(o, "name").empty())
            out[static_cast<uint32_t>(Num(o, "id"))] = Str(o, "name");
    return true;
}

Record RecipeRecord(const Recipe& r, const Record* output,
                    const std::map<uint32_t, std::string>& itemNames,
                    const std::map<uint32_t, std::string>& guildNames, Lang lang)
{
    Record rec;
    rec.type         = ChatLinks::LINK_RECIPE;
    rec.id           = r.id;
    rec.lang         = lang;
    rec.outputItemId = r.outputItemId;
    rec.outputCount  = static_cast<uint16_t>(r.outputCount);
    rec.minRating    = static_cast<uint16_t>(r.minRating);

    auto name = [](const std::map<uint32_t, std::string>& m, uint32_t id) {
        auto it = m.find(id);
        return it != m.end() ? it->second : "#" + std::to_string(id);
    };
    std::string out = output != nullptr ? output->name : name(itemNames, r.outputItemId);
    if (r.outputCount > 1) out = std::to_string(r.outputCount) + " " + out;
    rec.name = std::string(Labels::Tr("Recipe", lang)) + ": " + out;
    if (output != nullptr) {
        rec.icon   = output->icon;
        rec.rarity = output->rarity;
    }
    for (const auto& i : r.ingredients) {
        const std::string n = i.itemId ? name(itemNames, i.itemId) : name(guildNames, i.guildUpgradeId);
        rec.facts.push_back({"", std::to_string(i.count) + " " + n});
    }
    if (r.minRating > 0)
        rec.facts.push_back({"", std::string(Labels::Tr("Required Rating", lang)) + ": " + std::to_string(r.minRating)});
    return rec;
}

std::string WikiAskUrl(uint32_t id, const char* context)
{
    const std::string q = "[[Has game id::" + std::to_string(id) + "]][[Has context::" + context +
                          "]]|?Has canonical name|?Has game description|?Has skill facts";
    std::string enc;
    static const char kHex[] = "0123456789ABCDEF";
    for (unsigned char c : q) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            enc += static_cast<char>(c);
        } else {
            enc += '%';
            enc += kHex[c >> 4];
            enc += kHex[c & 15];
        }
    }
    return "https://wiki.guildwars2.com/api.php?action=ask&format=json&query=" + enc;
}

WikiResult WikiSkill(const std::string& body, Record& out)
{
    const json j = json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("query") || !j["query"].is_object()) return WikiResult::Error;
    const json& results = j["query"].contains("results") ? j["query"]["results"] : json();
    if (!results.is_object() || results.empty()) return WikiResult::Empty;   // none: [] or {}

    const json& first = results.begin().value();
    const json& pr = first.contains("printouts") ? first["printouts"] : json::object();
    auto firstString = [&](const char* key) {
        if (!pr.contains(key) || !pr[key].is_array() || pr[key].empty() || !pr[key][0].is_string()) return std::string();
        return pr[key][0].get<std::string>();
    };
    std::string name = Text::StripWikiMarkup(firstString("Has canonical name"));
    if (name.empty()) return WikiResult::Empty;            // never invent a name
    out.name        = name;
    out.description = Text::StripWikiMarkup(firstString("Has game description"));
    out.facts.clear();
    if (pr.contains("Has skill facts") && pr["Has skill facts"].is_array()) {
        for (const auto& blob : pr["Has skill facts"]) {
            if (!blob.is_string()) continue;
            const std::string s = blob.get<std::string>();
            size_t start = 0;
            while (start <= s.size()) {
                size_t end = s.find('\n', start);
                if (end == std::string::npos) end = s.size();
                const std::string line = s.substr(start, end - start);
                start = end + 1;
                if (line.find("wvw pvp") != std::string::npos) continue;   // competitive variant
                const std::string text = Text::StripWikiMarkup(line);
                if (!text.empty()) out.facts.push_back({"", text});
            }
        }
    }
    return WikiResult::Found;
}

}}} // namespace Trailpack::Core::Parse
