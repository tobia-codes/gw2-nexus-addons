#pragma once
// Parse.h — PURE parsers: GW2 API batch responses (`/v2/<endpoint>?ids=...`) and wiki SMW
// "ask" responses into Records. A body that is not the expected JSON (error page, wiki
// maintenance page, truncated download) makes the parser return false — never a half record.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "resolve/Record.h"

namespace Trailpack { namespace Core { namespace Parse {

// Batch endpoints: a JSON array of objects. Objects without id / name are skipped.
bool Items (const std::string& body, Lang lang, std::vector<Record>& out);
bool Skins (const std::string& body, Lang lang, std::vector<Record>& out);
bool Skills(const std::string& body, Lang lang, std::vector<Record>& out);
bool Traits(const std::string& body, Lang lang, std::vector<Record>& out);

// Recipes come in two steps: the recipe itself, then the names of its output and ingredients
// (one `/v2/items?ids=` batch) and of guild-upgrade ingredients (`/v2/guild/upgrades?ids=`).
struct Ingredient {
    uint32_t itemId         = 0;   // one of these two is set
    uint32_t guildUpgradeId = 0;
    int      count          = 0;
};
struct Recipe {
    uint32_t id           = 0;
    uint32_t outputItemId = 0;
    int      outputCount  = 1;
    int      minRating    = 0;
    std::vector<Ingredient> ingredients;
};
bool Recipes(const std::string& body, std::vector<Recipe>& out);

// The item ids a recipe needs names for (output first), and its guild-upgrade ids.
std::vector<uint32_t> RecipeItemIds(const Recipe& r);
std::vector<uint32_t> RecipeGuildIds(const Recipe& r);

// {id: name} of any batch response with "id" + "name" (items, guild upgrades).
bool Names(const std::string& body, std::map<uint32_t, std::string>& out);

// The recipe record: name "Recipe: <output> " (German "Rezept: ..."), the output's icon and
// rarity, one fact per ingredient ("3 Mithril Ingot"; unknown names as "#<id>") and the
// required rating. `output` may be null when the output item could not be resolved.
Record RecipeRecord(const Recipe& r, const Record* output,
                    const std::map<uint32_t, std::string>& itemNames,
                    const std::map<uint32_t, std::string>& guildNames, Lang lang);

// ── Wiki fallback (skills missing from /v2/skills, effect / buff links) ────────────────
// URL of the wiki "ask" query for a game id in `context` ("Skill" or "Effect"). The context
// is essential: game ids are shared across object types (skill 63440 is also an item).
std::string WikiAskUrl(uint32_t id, const char* context);

enum class WikiResult { Found, Empty, Error };
// Found: `out` gets name (canonical name, not the page title), description and facts (text
// only; the competitive variant of a fact is skipped). Empty: the wiki has no such page.
// Error: not an ask response (e.g. the maintenance page) — retry later.
WikiResult WikiSkill(const std::string& body, Record& out);

}}} // namespace Trailpack::Core::Parse
