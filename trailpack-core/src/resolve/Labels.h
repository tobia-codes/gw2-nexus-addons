#pragma once
// Labels.h — PURE English / German labels Core writes itself (item tooltip lines, recipe
// lines). Names and descriptions from the API are used as delivered in the requested language.

#include <string>

#include "core/Lang.h"

namespace Trailpack { namespace Core { namespace Labels {

// Our own fixed words: "Defense", "Weapon Strength", "Required Level", "Unused Infusion Slot",
// "Recipe", "Required Rating", ... (the English text is the key, like Memo's Tr).
const char* Tr(const char* en, Lang lang);

// API attribute key ("Power", "CritDamage", ...) -> display name ("Power", "Ferocity").
std::string Attribute(const std::string& key, Lang lang);

// API weight class ("Heavy") -> "Heavy" / "Schwer"; "" for anything else.
std::string WeightClass(const std::string& key, Lang lang);

// Item details type -> slot wording: "HelmAquatic" -> "Aquatic Helm", generic "*Aquatic"
// rule, "Default" -> "" (not a real slot), known slots translated, others verbatim.
std::string ItemSubtype(const std::string& key, Lang lang);

}}} // namespace Trailpack::Core::Labels
