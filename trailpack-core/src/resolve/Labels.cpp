// Labels.cpp — see Labels.h. German texts use the in-game wording where known.
#include "resolve/Labels.h"

#include <cstring>

namespace Trailpack { namespace Core { namespace Labels {

namespace {

struct Pair { const char* en; const char* de; };

const char* Lookup(const Pair* table, size_t n, const char* key, Lang lang)
{
    for (size_t i = 0; i < n; ++i)
        if (std::strcmp(table[i].en, key) == 0) return lang == Lang::De ? table[i].de : table[i].en;
    return nullptr;
}

const Pair kWords[] = {
    {"Defense",              "Verteidigung"},
    {"Weapon Strength",      "Waffenstärke"},
    {"Required Level",       "Benötigte Stufe"},
    {"Unused Infusion Slot", "Unbenutzter Infusionsplatz"},
    {"Unused Upgrade Slot",  "Unbenutzter Aufwertungsplatz"},
    {"Recipe",               "Rezept"},
    {"Required Rating",      "Benötigte Handwerksstufe"},
    {"Combo Field",          "Kombofeld"},
    {"Combo Finisher",       "Kombo-Finisher"},
    {"Breaks Stun",          "Betäubung brechen"},
};

// API attribute keys -> names. "CritDamage" is Ferocity in game, "ConditionDuration" Expertise,
// "BoonDuration" Concentration, "Healing" Healing Power.
const struct { const char* key; const char* en; const char* de; } kAttributes[] = {
    {"Power",             "Power",             "Kraft"},
    {"Precision",         "Precision",         "Präzision"},
    {"Toughness",         "Toughness",         "Zähigkeit"},
    {"Vitality",          "Vitality",          "Vitalität"},
    {"CritDamage",        "Ferocity",          "Wildheit"},
    {"ConditionDamage",   "Condition Damage",  "Zustandsschaden"},
    {"ConditionDuration", "Expertise",         "Fachkenntnis"},
    {"BoonDuration",      "Concentration",     "Konzentration"},
    {"Healing",           "Healing Power",     "Heilkraft"},
    {"AgonyResistance",   "Agony Resistance",  "Qual-Widerstand"},
};

const Pair kWeights[] = {
    {"Heavy", "Schwer"}, {"Medium", "Mittel"}, {"Light", "Leicht"}, {"Clothing", "Kleidung"},
};

// Slots that read better than the raw key, and their German names.
const struct { const char* key; const char* en; const char* de; } kSlots[] = {
    {"Helm",        "Helm",        "Kopfschutz"},
    {"HelmAquatic", "Aquatic Helm", "Atemgerät"},
    {"Shoulders",   "Shoulders",   "Schulterschutz"},
    {"Coat",        "Coat",        "Brustschutz"},
    {"Gloves",      "Gloves",      "Handschutz"},
    {"Leggings",    "Leggings",    "Beinschutz"},
    {"Boots",       "Boots",       "Schuhwerk"},
    {"Axe",         "Axe",         "Axt"},
    {"Dagger",      "Dagger",      "Dolch"},
    {"Mace",        "Mace",        "Streitkolben"},
    {"Pistol",      "Pistol",      "Pistole"},
    {"Scepter",     "Scepter",     "Zepter"},
    {"Sword",       "Sword",       "Schwert"},
    {"Focus",       "Focus",       "Fokus"},
    {"Shield",      "Shield",      "Schild"},
    {"Torch",       "Torch",       "Fackel"},
    {"Warhorn",     "Warhorn",     "Kriegshorn"},
    {"Greatsword",  "Greatsword",  "Großschwert"},
    {"Hammer",      "Hammer",      "Hammer"},
    {"LongBow",     "Longbow",     "Langbogen"},
    {"Rifle",       "Rifle",       "Gewehr"},
    {"ShortBow",    "Short Bow",   "Kurzbogen"},
    {"Staff",       "Staff",       "Stab"},
    {"Harpoon",     "Spear",       "Speer"},
    {"Speargun",    "Speargun",    "Harpunenschleuder"},
    {"Trident",     "Trident",     "Dreizack"},
    {"Amulet",      "Amulet",      "Amulett"},
    {"Ring",        "Ring",        "Ring"},
    {"Accessory",   "Accessory",   "Accessoire"},
};

} // namespace

const char* Tr(const char* en, Lang lang)
{
    const char* t = Lookup(kWords, sizeof kWords / sizeof kWords[0], en, lang);
    return t != nullptr ? t : en;
}

std::string Attribute(const std::string& key, Lang lang)
{
    for (const auto& a : kAttributes)
        if (key == a.key) return lang == Lang::De ? a.de : a.en;
    return key;
}

std::string WeightClass(const std::string& key, Lang lang)
{
    const char* t = Lookup(kWeights, sizeof kWeights / sizeof kWeights[0], key.c_str(), lang);
    return t != nullptr ? t : "";
}

std::string ItemSubtype(const std::string& key, Lang lang)
{
    if (key.empty() || key == "Default") return "";
    for (const auto& s : kSlots)
        if (key == s.key) return lang == Lang::De ? s.de : s.en;
    const std::string suffix = "Aquatic";
    if (key.size() > suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
        const std::string base = key.substr(0, key.size() - suffix.size());
        return lang == Lang::De ? ItemSubtype(base, lang) + " (Wasser)" : "Aquatic " + ItemSubtype(base, lang);
    }
    return key;
}

}}} // namespace Trailpack::Core::Labels
