// Trailpack Core host tests — pure logic only (no Nexus, no Win32, no real HTTP).
// API fixtures in tests/fixtures/ are real responses captured from api.guildwars2.com.
// The wiki was in maintenance when they were captured, so the SMW "ask" bodies below are
// hand-written in the documented response shape (PLAN, stage 3 notes) — replace them with
// captured ones once the wiki answers again.

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <set>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <cstddef>
#include <cstring>
#include <type_traits>

#include "TrailpackCoreApi.h"
#include "api/Service.h"
#include "core/I18n.h"
#include "core/Settings.h"
#include "chatlinks/ChatLinks.h"
#include "events/EventSchedule.h"
#include "generated/GameData.h"
#include "net/TokenBucket.h"
#include "resolve/Labels.h"
#include "resolve/Offline.h"
#include "resolve/Parse.h"
#include "resolve/Resolver.h"
#include "resolve/Text.h"

static int s_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " #cond "\n"; \
            ++s_failures; \
        } \
    } while (false)

#define CHECK_EQ(a, b) \
    do { \
        const auto _a = (a); const auto _b = (b); \
        if (!(_a == _b)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " #a " == " #b \
                      << "\n   got:      " << _a << "\n   expected: " << _b << "\n"; \
            ++s_failures; \
        } \
    } while (false)

using namespace Trailpack::Core;
namespace CL = Trailpack::ChatLinks;

// ── Helpers ─────────────────────────────────────────────────────────────────

static std::string Fixture(const std::string& name)
{
    std::string path = std::string(TPC_FIXTURES) + "/" + name;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { std::cerr << "missing fixture " << path << "\n"; ++s_failures; return ""; }
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

static const Record* ById(const std::vector<Record>& v, uint32_t id)
{
    for (const auto& r : v) if (r.id == id) return &r;
    return nullptr;
}

static bool HasFact(const Record& r, const std::string& text)
{
    for (const auto& f : r.facts) if (f.text == text) return true;
    return false;
}

static bool FactContains(const Record& r, const std::string& part)
{
    for (const auto& f : r.facts) if (f.text.find(part) != std::string::npos) return true;
    return false;
}

// Fake HTTP: the first route whose key is a substring of the URL answers. Records every URL.
struct FakeHttp {
    struct Route { std::string match; int status; std::string body; };
    std::vector<Route> routes;
    std::vector<std::string> urls;
    void On(const std::string& match, int status, const std::string& body) { routes.push_back({match, status, body}); }
    HttpFetch Fn() {
        return [this](const std::string& url, std::string& body) {
            urls.push_back(url);
            for (const auto& r : routes)
                if (url.find(r.match) != std::string::npos) { body = r.body; return r.status; }
            body = "{\"text\":\"all ids provided are invalid\"}";
            return 404;
        };
    }
    int Count(const std::string& part) const {
        int n = 0;
        for (const auto& u : urls) if (u.find(part) != std::string::npos) ++n;
        return n;
    }
};

// Drive the resolver like the worker thread would, with a fake clock, until idle.
static int64_t Drain(Resolver& r, int64_t now, int maxSteps = 500)
{
    for (int i = 0; i < maxSteps; ++i) {
        const int64_t next = r.Step(now);
        if (next >= now + 1000 && r.Stats(now).queued == 0) return now;
        now = std::max(next, now + 1);
    }
    return now;
}

static ResolverOptions FastOptions()
{
    ResolverOptions o;
    o.perSecond = 1000.0;     // tests are not about the budget (except the bucket tests)
    o.burst     = 1000.0;
    o.batchWindowMs = 0;
    return o;
}

// Hand-written SMW ask bodies (see the header comment).
static const char* kWikiSlam = R"J({"query":{"printrequests":[],"results":{"Slam (turtle)":{"printouts":{
  "Has canonical name":["Slam"],
  "Has game description":["Leap and knock nearby foes back."],
  "Has skill facts":[":<span class=\"inline-icon\">[[File:Damage.png|20px]]</span>&nbsp;[[Damage|Damage]]: 8,180 <span>(5.5)</span><sup><abbr title=\"x\">?</abbr></sup>\n:[[File:Range.png|20px]]&nbsp;[[Range|Range]]: 600\n:<span class=\"wvw pvp\">[[Range|Range]]: 300</span>\n:'''[[Defiance Break]]''': 200"]},
  "fulltext":"Slam (turtle)"}}}})J";
static const char* kWikiBlast = R"J({"query":{"results":{"Blast":{"printouts":{
  "Has canonical name":["Blast"],
  "Has game description":["'''Combo finisher''' &amp; more."],
  "Has skill facts":[":[[Combo field|Combo Field]]: Fire[[Category:Fire field skills]]"]},"fulltext":"Blast"}}}})J";
static const char* kWikiEffect = R"J({"query":{"results":{"Guild WvW Reward Track Boost (effect)":{"printouts":{
  "Has canonical name":["Guild WvW Reward Track Boost"],"Has game description":["+10% reward track progress."],
  "Has skill facts":[]},"fulltext":"Guild WvW Reward Track Boost (effect)"}}}})J";
static const char* kWikiEmpty = R"J({"query":{"printrequests":[],"results":[],"serializer":"SMW","version":2}})J";

// ── Offline data ────────────────────────────────────────────────────────────

static void test_offline_build_label()
{
    CL::BuildLink b;
    b.profession = CL::PROF_MESMER;
    b.specs[0].id = 1;    // Dueling
    b.specs[2].id = 59;   // Mirage (elite)
    Record r;
    CHECK(Offline::Build(CL::EncodeBuild(b), Lang::En, r));
    CHECK_EQ(r.name, std::string("Mirage Build"));
    CHECK(HasFact(r, "Dueling") && HasFact(r, "Mirage"));
    CHECK(Offline::Build(CL::EncodeBuild(b), Lang::De, r));
    CHECK_EQ(r.name, std::string("Illusionist-Build"));

    CL::BuildLink core;
    core.profession = CL::PROF_MESMER;
    core.specs[0].id = 1;
    CHECK(Offline::Build(CL::EncodeBuild(core), Lang::En, r));
    CHECK_EQ(r.name, std::string("Mesmer Build"));
    CHECK(!Offline::Build(CL::EncodeItem(1), Lang::En, r));
}

static void test_offline_waypoint()
{
    Record r;
    CHECK(Offline::Poi(3, Lang::En, r));
    CHECK_EQ(r.name, std::string("Fort Salma Waypoint"));
    CHECK(!r.mapName.empty());
    CHECK(r.poiKind == GameData::POI_WAYPOINT);
    CHECK(r.type == CL::LINK_MAP);
    CHECK(Offline::Poi(3, Lang::De, r));
    CHECK_EQ(r.name, std::string("Wegmarke Fort Salma"));
    CHECK(Offline::Poi(2524, Lang::En, r) && r.poiKind == GameData::POI_VISTA);
    CHECK(!Offline::Poi(0, Lang::En, r));
    CHECK(!Offline::Poi(4000000, Lang::En, r));
    // The table is sorted (binary search relies on it) and every PoI has a map.
    for (size_t i = 1; i < GameData::kPoiCount; ++i) CHECK(GameData::kPois[i - 1].id < GameData::kPois[i].id);
    for (size_t i = 0; i < GameData::kPoiCount; ++i) CHECK(GameData::kPois[i].map < GameData::kMapCount);
}

// ── Items ───────────────────────────────────────────────────────────────────

static void test_item_armor_tooltip()
{
    std::vector<Record> v;
    CHECK(Parse::Items(Fixture("items_en.json"), Lang::En, v));
    const Record* r = ById(v, 48073);   // Zojja's Breastplate
    CHECK(r != nullptr);
    if (!r) return;
    CHECK_EQ(r->name, std::string("Zojja's Breastplate"));
    CHECK(r->rarity == Rarity::Ascended);
    CHECK(HasFact(*r, "Defense: 381"));
    CHECK(HasFact(*r, "+141 Power"));
    CHECK(HasFact(*r, "+101 Ferocity"));          // CritDamage is Ferocity in game
    CHECK(!FactContains(*r, "CritDamage"));
    CHECK(HasFact(*r, "Unused Infusion Slot"));
    CHECK(HasFact(*r, "Coat"));
    CHECK(HasFact(*r, "Heavy"));
    CHECK(HasFact(*r, "Required Level: 80"));
    CHECK_EQ(r->description, std::string("Crafted in the style of the renowned asuran genius, Zojja."));
    CHECK(r->bound == Bound::AccountOnAcquire);
    CHECK(!r->tradeable);
}

static void test_item_weapon_tooltip()
{
    std::vector<Record> v;
    CHECK(Parse::Items(Fixture("items_en.json"), Lang::En, v));
    const Record* r = ById(v, 30680);   // Judgment Trident
    CHECK(r != nullptr);
    if (!r) return;
    CHECK(HasFact(*r, "Weapon Strength: 617 - 681"));
    CHECK(!FactContains(*r, "Defense"));
    CHECK(HasFact(*r, "Trident"));
    CHECK(r->description.find('<') == std::string::npos);
    CHECK(r->description.find('\n') != std::string::npos);   // <br> became a line break
    CHECK(r->description.find("\"Great for") == 0);
    CHECK(r->bound == Bound::SoulOnAcquire);
    CHECK(r->noSell);
}

static void test_item_rune_bonuses()
{
    std::vector<Record> v;
    CHECK(Parse::Items(Fixture("items_en.json"), Lang::En, v));
    const Record* r = ById(v, 24836);   // Superior Rune of the Scholar
    CHECK(r != nullptr);
    if (!r) return;
    CHECK(HasFact(*r, "(1): +25 Power"));
    CHECK(HasFact(*r, "(6): +125 Ferocity"));
    CHECK_EQ(r->description, std::string("Element: Brilliance\nDouble-click to apply to a piece of armor."));
    const Record* sigil = ById(v, 24615);   // Superior Sigil of Force: the buff text
    CHECK(sigil != nullptr && HasFact(*sigil, "+5% Damage"));
}

static void test_item_nosell_tradeable()
{
    std::vector<Record> v;
    CHECK(Parse::Items(Fixture("items_en.json"), Lang::En, v));
    const Record* r = ById(v, 19976);   // Mystic Coin: NoSell, yet traded on the TP
    CHECK(r != nullptr);
    if (!r) return;
    CHECK(r->noSell);
    CHECK(r->tradeable);
    CHECK(r->bound == Bound::None);
    CHECK(r->vendorValue == 50);
}

static void test_item_rarity_values()
{
    const char* names[] = {"Junk", "Basic", "Fine", "Masterwork", "Rare", "Exotic", "Ascended", "Legendary"};
    std::string body = "[";
    for (int i = 0; i < 8; ++i)
        body += std::string(i ? "," : "") + "{\"id\":" + std::to_string(i + 1) + ",\"name\":\"x\",\"rarity\":\"" + names[i] + "\"}";
    body += ",{\"id\":20,\"name\":\"x\",\"rarity\":\"Mythic\"},{\"id\":21,\"name\":\"x\"}]";
    std::vector<Record> v;
    CHECK(Parse::Items(body, Lang::En, v));
    for (int i = 0; i < 8; ++i) CHECK(ById(v, i + 1)->rarity == Rarity(i + 1));
    CHECK(ById(v, 20)->rarity == Rarity::Unknown);
    CHECK(ById(v, 21)->rarity == Rarity::Unknown);
}

static void test_item_subtype_wording()
{
    CHECK_EQ(Labels::ItemSubtype("HelmAquatic", Lang::En), std::string("Aquatic Helm"));
    CHECK_EQ(Labels::ItemSubtype("GauntletsAquatic", Lang::En), std::string("Aquatic Gauntlets"));
    CHECK_EQ(Labels::ItemSubtype("Default", Lang::En), std::string(""));
    CHECK_EQ(Labels::ItemSubtype("Coat", Lang::En), std::string("Coat"));
    CHECK_EQ(Labels::ItemSubtype("Rifle", Lang::En), std::string("Rifle"));
    CHECK_EQ(Labels::ItemSubtype("Trident", Lang::En), std::string("Trident"));
    CHECK_EQ(Labels::ItemSubtype("Speargun", Lang::En), std::string("Speargun"));
    CHECK_EQ(Labels::ItemSubtype("SomethingNew", Lang::En), std::string("SomethingNew"));
}

static void test_item_german()
{
    std::vector<Record> v;
    CHECK(Parse::Items(Fixture("items_de.json"), Lang::De, v));
    const Record* r = ById(v, 48073);
    CHECK(r != nullptr);
    if (!r) return;
    CHECK_EQ(r->name, std::string("Zojja-Brustplatte"));   // API name used verbatim
    CHECK(r->lang == Lang::De);
    CHECK(HasFact(*r, "Verteidigung: 381"));
    CHECK(HasFact(*r, "+101 Wildheit"));
    CHECK(HasFact(*r, "Benötigte Stufe: 80"));
    CHECK(HasFact(*r, "Schwer"));
    const Record* rune = ById(v, 24836);
    CHECK(rune != nullptr && FactContains(*rune, "(1): +25 Kraft"));
}

static void test_text_helpers()
{
    CHECK_EQ(Text::StripGameMarkup("<c=@flavor>a<br>b</c>"), std::string("a\nb"));
    CHECK_EQ(Text::StripWikiMarkup(":[[File:x.png|20px]]&nbsp;[[Damage|Hit]]: 5<sup>?</sup>"), std::string("Hit: 5"));
    CHECK_EQ(Text::StripWikiMarkup("'''Bold''' [[Link]] [[Category:Foo]]"), std::string("Bold Link"));
    // Truncation never cuts a UTF-8 character: "ä" is two bytes.
    const std::string s = "ab\xC3\xA4" "cd";
    CHECK_EQ(Text::TruncateUtf8(s, 3), std::string("ab"));
    CHECK_EQ(Text::TruncateUtf8(s, 4), std::string("ab\xC3\xA4"));
    CHECK_EQ(Text::TruncateUtf8(s, 99), s);
}

// ── Skills / wiki ───────────────────────────────────────────────────────────

static void test_skill_parse()
{
    std::vector<Record> v;
    CHECK(Parse::Skills(Fixture("skills_en.json"), Lang::En, v));
    const Record* r = ById(v, 5516);
    CHECK(r != nullptr);
    if (!r) return;
    CHECK_EQ(r->name, std::string("Conjure Fiery Greatsword"));
    CHECK(!r->icon.empty());
    CHECK(HasFact(*r, "Range: 1200"));
    CHECK(HasFact(*r, "Recharge: 180s"));
    CHECK(HasFact(*r, "Damage"));
    CHECK(HasFact(*r, "Burning (3s): Deals damage every second; stacks intensity."));
    CHECK(HasFact(*r, "Duration: 30s"));
    CHECK(HasFact(*r, "Radius: 150"));
    for (const auto& f : r->facts) CHECK(!f.icon.empty());   // API facts carry icons
    std::vector<Record> de;
    CHECK(Parse::Skills(Fixture("skills_de.json"), Lang::De, de));
    CHECK(!de.empty() && de[0].name != r->name);              // German name from the API
}

static void test_wiki_fallback_parse()
{
    Record r;
    CHECK(Parse::WikiSkill(kWikiSlam, r) == Parse::WikiResult::Found);
    CHECK_EQ(r.name, std::string("Slam"));                     // canonical name, not the page title
    CHECK_EQ(r.description, std::string("Leap and knock nearby foes back."));
    CHECK(HasFact(r, "Damage: 8,180 (5.5)"));
    CHECK(HasFact(r, "Range: 600"));
    CHECK(!HasFact(r, "Range: 300"));                          // competitive variant skipped
    CHECK(HasFact(r, "Defiance Break: 200"));
    for (const auto& f : r.facts) CHECK(f.icon.empty());       // wiki facts are text only

    CHECK(Parse::WikiSkill(kWikiBlast, r) == Parse::WikiResult::Found);
    CHECK_EQ(r.description, std::string("Combo finisher & more."));
    CHECK(HasFact(r, "Combo Field: Fire"));
    CHECK(!FactContains(r, "Category"));

    CHECK(Parse::WikiSkill(kWikiEffect, r) == Parse::WikiResult::Found);
    CHECK_EQ(r.name, std::string("Guild WvW Reward Track Boost"));

    Record e;
    CHECK(Parse::WikiSkill(kWikiEmpty, e) == Parse::WikiResult::Empty);
    CHECK(e.name.empty());                                     // never invent a name
    CHECK(Parse::WikiSkill(Fixture("wiki_maintenance.html"), e) == Parse::WikiResult::Error);
}

static void test_wiki_url_context()
{
    const std::string u = Parse::WikiAskUrl(63440, "Skill");   // also an item: "Defender's Staff"
    CHECK(u.find("63440") != std::string::npos);
    CHECK(u.find("Has%20context%3A%3ASkill") != std::string::npos);
    CHECK(Parse::WikiAskUrl(35126, "Effect").find("Has%20context%3A%3AEffect") != std::string::npos);
    CHECK(u.find("https://wiki.guildwars2.com/") == 0);
}

static void test_resolver_skill_fallback_paths()
{
    FakeHttp http;
    http.On("/v2/build", 200, "{\"id\":1}");
    http.On("skills?ids=", 404, "{\"text\":\"all ids provided are invalid\"}");
    http.On("63475%5D%5D%5B%5BHas%20context%3A%3ASkill", 200, kWikiSlam);
    http.On("35126%5D%5D%5B%5BHas%20context%3A%3ASkill", 200, kWikiEmpty);
    http.On("35126%5D%5D%5B%5BHas%20context%3A%3AEffect", 200, kWikiEffect);
    http.On("99999", 200, kWikiEmpty);
    Resolver res(http.Fn(), FastOptions());
    int64_t now = 1000;
    CHECK(res.Resolve(CL::LINK_SKILL, 63475, Lang::En, now, nullptr) == Answer::Pending);
    CHECK(res.Resolve(CL::LINK_SKILL, 35126, Lang::En, now, nullptr) == Answer::Pending);
    CHECK(res.Resolve(CL::LINK_SKILL, 99999, Lang::En, now, nullptr) == Answer::Pending);
    now = Drain(res, now);
    const auto done = res.TakeCompletions();
    CHECK(done.size() == 3);
    Record r;
    CHECK(res.Resolve(CL::LINK_SKILL, 63475, Lang::En, now, &r) == Answer::Warm);
    CHECK_EQ(r.name, std::string("Slam"));
    CHECK(res.Resolve(CL::LINK_SKILL, 35126, Lang::En, now, &r) == Answer::Warm);
    CHECK_EQ(r.name, std::string("Guild WvW Reward Track Boost"));
    CHECK(res.Resolve(CL::LINK_SKILL, 99999, Lang::En, now, &r) == Answer::Failed);   // no invented name
    CHECK(http.Count("skills?ids=") == 1);                      // all three in one batch
}

static void test_resolver_wiki_maintenance_fails_then_retries()
{
    FakeHttp http;
    http.On("/v2/build", 200, "{\"id\":1}");
    http.On("wiki.guildwars2.com", 200, Fixture("wiki_maintenance.html"));
    ResolverOptions o = FastOptions();
    Resolver res(http.Fn(), o);
    int64_t now = 1000;
    CHECK(res.Resolve(CL::LINK_SKILL, 63475, Lang::En, now, nullptr) == Answer::Pending);
    now = Drain(res, now, 2000);
    const auto done = res.TakeCompletions();
    CHECK(done.size() == 1 && !done[0].ok);
    CHECK(http.Count("wiki.guildwars2.com") == o.maxAttempts);
    CHECK(!res.Stats(now).lastError.empty());
    CHECK(res.Resolve(CL::LINK_SKILL, 63475, Lang::En, now, nullptr) == Answer::Failed);   // cooldown
}

// ── Traits, recipes, skins ──────────────────────────────────────────────────

static void test_trait_parse()
{
    std::vector<Record> v;
    CHECK(Parse::Traits(Fixture("traits_en.json"), Lang::En, v));
    const Record* r = ById(v, 214);
    CHECK(r != nullptr);
    if (!r) return;
    CHECK_EQ(r->name, std::string("Raging Storm"));
    CHECK_EQ(r->description, std::string("Critically striking a foe grants fury. Gain ferocity while under the effects of fury."));
    CHECK(!r->icon.empty());
    CHECK(HasFact(*r, "Recharge: 8s"));                        // Recharge reads "value"
    CHECK(HasFact(*r, "Radius: 360"));                         // Distance reads "distance"
    CHECK(HasFact(*r, "Number of Targets: 5"));
    CHECK(HasFact(*r, "Fury (4s): Critical chance increased; stacks duration."));
    CHECK(HasFact(*r, "+180 Ferocity"));                       // AttributeAdjust without text
    for (const auto& f : r->facts) CHECK(!f.icon.empty());
}

static FakeHttp RecipeHttp()
{
    FakeHttp http;
    http.On("/v2/build", 200, "{\"id\":1}");
    http.On("recipes?ids=", 200, Fixture("recipes.json"));
    http.On("items?ids=62905,62948,62901,19721&lang=en", 200, Fixture("recipe_items_en.json"));
    http.On("items?ids=77721,70489&lang=en", 200, Fixture("recipe_items_en.json"));
    http.On("items?ids=62905,62948,62901,19721&lang=de", 200, Fixture("recipe_items_de.json"));
    http.On("guild/upgrades?ids=706&lang=en", 200, Fixture("guild_upgrades_en.json"));
    return http;
}

static void test_recipe_resolve()
{
    FakeHttp http = RecipeHttp();
    Resolver res(http.Fn(), FastOptions());
    int64_t now = 1000;
    CHECK(res.Resolve(CL::LINK_RECIPE, 9508, Lang::En, now, nullptr) == Answer::Pending);
    CHECK(res.Resolve(CL::LINK_RECIPE, 11777, Lang::En, now, nullptr) == Answer::Pending);
    now = Drain(res, now);
    Record r;
    CHECK(res.Resolve(CL::LINK_RECIPE, 9508, Lang::En, now, &r) == Answer::Warm);
    CHECK_EQ(r.name, std::string("Recipe: Elegant Huntsman's Backpack"));
    CHECK(r.outputItemId == 62905);
    CHECK(r.rarity == Rarity::Exotic);
    CHECK(!r.icon.empty());
    CHECK(HasFact(r, "1 Elegant Huntsman's Tools"));
    CHECK(HasFact(r, "3 Glob of Ectoplasm"));
    CHECK(HasFact(r, "Required Rating: 400"));
    // Output and ingredients came in ONE items batch per recipe.
    CHECK(http.Count("items?ids=62905,62948,62901,19721") == 1);

    CHECK(res.Resolve(CL::LINK_RECIPE, 11777, Lang::En, now, &r) == Answer::Warm);
    CHECK(r.outputItemId == 77721 && r.minRating == 350);
    CHECK(HasFact(r, "10 Bronze Tequatl Trophy"));            // guild-upgrade ingredient
    CHECK(http.Count("guild/upgrades?ids=706") == 1);
    // The fetched items are warm for item links, too.
    CHECK(res.Resolve(CL::LINK_ITEM, 19721, Lang::En, now, &r) == Answer::Warm);
}

static void test_recipe_localized()
{
    FakeHttp http = RecipeHttp();
    Resolver res(http.Fn(), FastOptions());
    int64_t now = 1000;
    res.Resolve(CL::LINK_RECIPE, 9508, Lang::De, now, nullptr);
    now = Drain(res, now);
    Record r;
    CHECK(res.Resolve(CL::LINK_RECIPE, 9508, Lang::De, now, &r) == Answer::Warm);
    CHECK_EQ(r.name, std::string("Rezept: Eleganter Waidmann-Rucksack"));
    CHECK(FactContains(r, "Benötigte Handwerksstufe: 400"));
}

static void test_skin_parse_and_lang()
{
    std::vector<Record> v;
    CHECK(Parse::Skins(Fixture("skins_en.json"), Lang::En, v));
    CHECK(v.size() == 1 && v[0].id == 8585 && !v[0].name.empty() && !v[0].icon.empty());
    CHECK(HasFact(v[0], "Pistol"));

    FakeHttp http;
    http.On("/v2/build", 200, "{\"id\":1}");
    http.On("skins?ids=8585&lang=de", 200, Fixture("skins_de.json"));
    Resolver res(http.Fn(), FastOptions());
    res.Resolve(CL::LINK_SKIN, 8585, Lang::De, 1000, nullptr);
    Drain(res, 1000);
    CHECK(http.Count("skins?ids=8585&lang=de") == 1);
}

// ── Cache, language, lifetime ───────────────────────────────────────────────

static FakeHttp ItemHttp()
{
    FakeHttp http;
    http.On("/v2/build", 200, "{\"id\":7}");
    http.On("&lang=en", 200, Fixture("items_en.json"));
    http.On("&lang=de", 200, Fixture("items_de.json"));
    return http;
}

static void test_cache_roundtrip()
{
    FakeHttp http = ItemHttp();
    Resolver a(http.Fn(), FastOptions());
    a.Resolve(CL::LINK_ITEM, 48073, Lang::En, 1000, nullptr);
    a.Resolve(CL::LINK_ITEM, 19976, Lang::En, 1000, nullptr);
    Drain(a, 1000);
    const auto dirty = a.TakeDirty();
    CHECK(dirty.size() == 1 && dirty[0].first == Lang::En && dirty[0].second == CL::LINK_ITEM);
    const std::string saved = a.SaveCache(Lang::En, CL::LINK_ITEM);

    FakeHttp none;
    Resolver b(none.Fn(), FastOptions());
    CHECK(b.LoadCache(Lang::En, CL::LINK_ITEM, saved));
    Record r;
    CHECK(b.Resolve(CL::LINK_ITEM, 48073, Lang::En, 2000, &r) == Answer::Warm);
    CHECK_EQ(r.name, std::string("Zojja's Breastplate"));
    CHECK(r.rarity == Rarity::Ascended && r.bound == Bound::AccountOnAcquire && HasFact(r, "Defense: 381"));
    CHECK(b.Resolve(CL::LINK_ITEM, 19976, Lang::En, 2000, &r) == Answer::Warm && r.tradeable && r.noSell);

    // Unknown version: refused (entries are simply refetched), nothing poisoned.
    Resolver c(none.Fn(), FastOptions());
    CHECK(!c.LoadCache(Lang::En, CL::LINK_ITEM, "{\"version\":999,\"entries\":[]}"));
    CHECK(!c.LoadCache(Lang::En, CL::LINK_ITEM, "garbage"));
    // A missing field reads back as its default.
    CHECK(c.LoadCache(Lang::En, CL::LINK_ITEM, "{\"version\":1,\"entries\":[{\"id\":5,\"n\":\"Old\"}]}"));
    CHECK(c.Resolve(CL::LINK_ITEM, 5, Lang::En, 2000, &r) == Answer::Warm && r.rarity == Rarity::Unknown);
}

static void test_language_namespacing()
{
    FakeHttp http = ItemHttp();
    Resolver res(http.Fn(), FastOptions());
    res.Resolve(CL::LINK_ITEM, 48073, Lang::En, 1000, nullptr);
    Drain(res, 1000);
    Record r;
    CHECK(res.Resolve(CL::LINK_ITEM, 48073, Lang::En, 2000, &r) == Answer::Warm);
    // A German request never gets the warm English entry.
    CHECK(res.Resolve(CL::LINK_ITEM, 48073, Lang::De, 2000, &r) == Answer::Pending);
    Drain(res, 2000);
    CHECK(res.Resolve(CL::LINK_ITEM, 48073, Lang::De, 3000, &r) == Answer::Warm);
    CHECK_EQ(r.name, std::string("Zojja-Brustplatte"));
    CHECK(http.Count("&lang=de") == 1);
}

static void test_invalid_id_and_type()
{
    FakeHttp http = ItemHttp();
    Resolver res(http.Fn(), FastOptions());
    CHECK(res.Resolve(CL::LINK_ITEM, 0, Lang::En, 1000, nullptr) == Answer::Failed);
    CHECK(res.Resolve(CL::LINK_MAP, 3, Lang::En, 1000, nullptr) == Answer::Failed);   // offline type
    Drain(res, 1000);
    CHECK(http.Count("items") == 0);
    CHECK(res.TakeCompletions().empty());
}

static void test_terminal_outcome_invariant()
{
    // Resolved, unknown (404), transient failure until give-up, and failure in cooldown:
    // every Pending answer gets exactly one completion, synchronous answers get none.
    FakeHttp http;
    http.On("/v2/build", 200, "{\"id\":1}");
    http.On("items?ids=48073", 200, Fixture("items_en.json"));
    http.On("skins?ids=", 503, "busy");
    http.On("traits?ids=", 404, "{\"text\":\"all ids provided are invalid\"}");
    Resolver res(http.Fn(), FastOptions());
    int64_t now = 1000;
    int pending = 0, sync = 0;
    auto ask = [&](uint8_t t, uint32_t id) {
        Answer a = res.Resolve(t, id, Lang::En, now, nullptr);
        (a == Answer::Pending ? pending : sync)++;
    };
    ask(CL::LINK_ITEM, 48073);
    ask(CL::LINK_SKIN, 1);
    ask(CL::LINK_TRAIT, 2);
    ask(CL::LINK_ITEM, 0);
    now = Drain(res, now, 5000);
    ask(CL::LINK_TRAIT, 2);                 // failed, in cooldown -> synchronous
    std::map<std::pair<int, uint32_t>, int> count;
    for (const auto& c : res.TakeCompletions()) count[{c.record.type, c.record.id}]++;
    CHECK(pending == 3);
    CHECK(sync == 2);
    CHECK(count.size() == 3);
    for (const auto& kv : count) CHECK(kv.second == 1);
}

static void test_failure_cooldown()
{
    FakeHttp http;
    http.On("/v2/build", 200, "{\"id\":1}");
    ResolverOptions o = FastOptions();
    o.failCooldownMs = 10000;
    Resolver res(http.Fn(), o);
    res.Resolve(CL::LINK_ITEM, 42, Lang::En, 1000, nullptr);   // 404 -> failed
    Drain(res, 1000);
    CHECK(http.Count("items?ids=42") == 1);
    CHECK(res.Resolve(CL::LINK_ITEM, 42, Lang::En, 5000, nullptr) == Answer::Failed);
    Drain(res, 5000);
    CHECK(http.Count("items?ids=42") == 1);                      // not refetched in cooldown
    CHECK(res.Resolve(CL::LINK_ITEM, 42, Lang::En, 12000, nullptr) == Answer::Pending);
    Drain(res, 12000);
    CHECK(http.Count("items?ids=42") == 2);
}

static void test_batching_and_window()
{
    FakeHttp http;
    http.On("/v2/build", 200, "{\"id\":1}");
    http.On("items?ids=", 200, "[]");
    ResolverOptions o = FastOptions();
    o.batchWindowMs = 50;
    Resolver res(http.Fn(), o);
    for (uint32_t id = 1; id <= 250; ++id) res.Resolve(CL::LINK_ITEM, id, Lang::En, 1000, nullptr);
    res.Step(1000);                                               // /v2/build
    res.Step(1010);                                               // inside the window: no batch yet
    CHECK(http.Count("items?ids=") == 0);
    Drain(res, 1060);
    CHECK(http.Count("items?ids=") == 2);                         // 200 + 50 ids
    CHECK(http.urls.size() == 3);
}

static void test_stale_refresh_on_new_build()
{
    FakeHttp http = ItemHttp();
    Resolver res(http.Fn(), FastOptions());
    // Entry cached under build 5; the game is now at build 7 (see ItemHttp).
    CHECK(res.LoadCache(Lang::En, CL::LINK_ITEM,
          "{\"version\":1,\"entries\":[{\"id\":48073,\"n\":\"Old name\",\"at\":1000,\"b\":5}]}"));
    res.Step(1000);                                               // learns build 7
    Record r;
    CHECK(res.Resolve(CL::LINK_ITEM, 48073, Lang::En, 2000, &r) == Answer::Warm);
    CHECK_EQ(r.name, std::string("Old name"));                    // served while refreshing
    Drain(res, 2000);
    const auto done = res.TakeCompletions();
    CHECK(done.size() == 1 && done[0].ok && done[0].record.name == "Zojja's Breastplate");
    CHECK(res.Resolve(CL::LINK_ITEM, 48073, Lang::En, 3000, &r) == Answer::Warm && r.name == "Zojja's Breastplate");
    // Expired by age (30 days) refreshes, too.
    const int64_t later = 3000 + 31LL * 24 * 3600 * 1000;
    CHECK(res.Resolve(CL::LINK_ITEM, 48073, Lang::En, later, &r) == Answer::Warm);
    CHECK(res.Stats(later).queued == 1);
}

static void test_offline_mode()
{
    FakeHttp http = ItemHttp();
    Resolver res(http.Fn(), FastOptions());
    CHECK(res.Resolve(CL::LINK_ITEM, 48073, Lang::En, 1000, nullptr) == Answer::Pending);
    res.SetOffline(true);
    const auto done = res.TakeCompletions();
    CHECK(done.size() == 1 && !done[0].ok);                       // nothing stays pending
    CHECK(res.Resolve(CL::LINK_ITEM, 19976, Lang::En, 1000, nullptr) == Answer::Failed);
    Drain(res, 1000);
    CHECK(http.urls.empty());                                     // no request at all
    res.SetOffline(false);
    CHECK(res.Resolve(CL::LINK_ITEM, 19976, Lang::En, 1000, nullptr) == Answer::Pending);
}

static void test_teardown_awaits_inflight_fetch()
{
    std::atomic<bool> inFetch{false}, release{false};
    HttpFetch slow = [&](const std::string& url, std::string& body) {
        if (url.find("/v2/build") != std::string::npos) { body = "{\"id\":1}"; return 200; }
        inFetch = true;
        while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        body = Fixture("items_en.json");
        return 200;
    };
    Resolver res(slow, FastOptions());
    res.Resolve(CL::LINK_ITEM, 48073, Lang::En, 1000, nullptr);
    res.Step(1000);                                               // build
    std::thread worker([&] { res.Step(1001); });                  // blocks in the item fetch
    while (!inFetch) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::atomic<bool> shutdownDone{false};
    std::thread closer([&] { res.Shutdown(); shutdownDone = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(!shutdownDone);                                         // waits for the request
    release = true;
    worker.join();
    closer.join();
    CHECK(shutdownDone);
    CHECK(res.TakeCompletions().empty());                         // nothing emitted afterwards
    CHECK(res.Resolve(CL::LINK_ITEM, 48073, Lang::En, 2000, nullptr) == Answer::Failed);
    res.Step(2000);                                               // safe no-op
}

static void test_token_bucket()
{
    TokenBucket b(2.0, 3.0);
    CHECK(b.TryTake(0) && b.TryTake(0) && b.TryTake(0));
    CHECK(!b.TryTake(0));                                         // burst used up
    CHECK(b.NextTokenAt(0) >= 500 && b.NextTokenAt(0) <= 501);   // 2 per second
    CHECK(!b.TryTake(400));
    CHECK(b.TryTake(501));
    b.Backoff(1000);
    CHECK(b.PausedUntil() == 1000 + TokenBucket::kFirstBackoffMs);
    CHECK(!b.TryTake(1500));
    b.Backoff(3000);
    CHECK(b.PausedUntil() == 3000 + 2 * TokenBucket::kFirstBackoffMs);   // doubles
    b.Success();
    b.Backoff(10000);
    CHECK(b.PausedUntil() == 10000 + TokenBucket::kFirstBackoffMs);      // reset
}

static void test_resolver_respects_budget()
{
    FakeHttp http;
    http.On("/v2/build", 200, "{\"id\":1}");
    ResolverOptions o;                                            // real defaults: 2/s, burst 20
    o.batchWindowMs = 0;
    Resolver res(http.Fn(), o);
    // 40 different types/langs can't share a batch: one request each.
    for (uint32_t id = 1; id <= 20; ++id) {
        res.Resolve(CL::LINK_ITEM, id, id % 2 ? Lang::En : Lang::De, 0, nullptr);
        res.Resolve(CL::LINK_SKIN, id, id % 2 ? Lang::En : Lang::De, 0, nullptr);
    }
    for (int i = 0; i < 100; ++i) res.Step(0);
    CHECK(http.urls.size() <= 20);                                // burst
    for (int64_t t = 0; t <= 1000; t += 10) res.Step(t);
    CHECK(http.urls.size() <= 22);                                // + 2 per second
    CHECK(res.Stats(1000).requestsLastMinute == (int)http.urls.size());
}


// ── ABI (public/TrailpackCoreApi.h) ─────────────────────────────────────────

static void test_abi_layout()
{
    // POD, trivially copyable, layout pinned: a change here breaks every consumer.
    CHECK(std::is_trivially_copyable<TpcRecord>::value && std::is_standard_layout<TpcRecord>::value);
    CHECK(std::is_trivially_copyable<TpcEvent>::value && std::is_standard_layout<TpcEvent>::value);
    CHECK(std::is_standard_layout<TpcApi>::value);
    CHECK(offsetof(TpcRecord, structSize) == 0);
    CHECK(offsetof(TpcRecord, linkType) == 4);
    CHECK(offsetof(TpcRecord, id) == 8);
    CHECK(offsetof(TpcRecord, name) == 12);
    CHECK(offsetof(TpcRecord, iconUrl) == 140);
    CHECK(offsetof(TpcRecord, description) == 396);
    CHECK(offsetof(TpcRecord, factCount) == 908);
    CHECK(offsetof(TpcRecord, facts) == 912);
    CHECK(offsetof(TpcRecord, rarity) == 912 + 16 * 288);
    CHECK(offsetof(TpcRecord, recipeOutputItemId) == 5528);
    CHECK(offsetof(TpcRecord, poiMapName) == 5536);
    CHECK(sizeof(TpcRecord) == 5636);
    CHECK(offsetof(TpcEvent, structSize) == 0 && offsetof(TpcEvent, id) == 4);
    CHECK(sizeof(TpcOccurrence) == 24);
    CHECK(offsetof(TpcApi, apiVersion) == 0 && offsetof(TpcApi, Resolve) == 8);
}

static void test_abi_fill_respects_struct_size()
{
    Record r;
    r.type = CL::LINK_ITEM; r.id = 7; r.name = "Name"; r.icon = "https://x/i.png";
    r.rarity = Rarity::Exotic;
    // An "old consumer" whose struct ends after iconUrl: nothing beyond it is written.
    unsigned char buf[sizeof(TpcRecord)];
    std::memset(buf, 0xAB, sizeof buf);
    auto* rec = reinterpret_cast<TpcRecord*>(buf);
    rec->structSize = offsetof(TpcRecord, description);
    CHECK(FillRecord(r, TPC_RESOLVED, rec));
    CHECK(rec->structSize == offsetof(TpcRecord, description));
    CHECK(std::string(rec->name) == "Name" && rec->status == TPC_RESOLVED && rec->id == 7);
    CHECK(buf[offsetof(TpcRecord, description)] == 0xAB);       // untouched
    // Too small for the head: refused.
    TpcRecord tiny; tiny.structSize = 4;
    CHECK(!FillRecord(r, TPC_RESOLVED, &tiny));
    CHECK(!FillRecord(r, TPC_RESOLVED, nullptr));
}

static void test_abi_fill_caps()
{
    Record r;
    r.type = CL::LINK_SKILL; r.id = 1;
    r.name = std::string(126, 'a') + "\xC3\xA4" "zz";          // "ä" straddles the 127-byte limit
    for (int i = 0; i < 20; ++i) r.facts.push_back({"", "fact " + std::to_string(i)});
    TpcRecord rec; rec.structSize = sizeof rec;
    CHECK(FillRecord(r, TPC_RESOLVED, &rec));
    CHECK(std::strlen(rec.name) == 126);                         // cut before the "ä", not inside it
    CHECK(rec.factCount == TPC_MAX_FACTS);
    CHECK(std::string(rec.facts[15].text) == "fact 15");
}

static void test_service_end_to_end()
{
    FakeHttp http = ItemHttp();
    Service svc(http.Fn(), FastOptions());
    svc.SetNow(1000);
    svc.SetLang(Lang::De);
    TpcRecord rec; rec.structSize = sizeof rec;

    // Built in: synchronous, in the active language.
    CHECK(svc.Resolve(CL::LINK_MAP, 3, nullptr, &rec) == TPC_RESOLVED);
    CHECK(std::string(rec.name) == "Wegmarke Fort Salma" && rec.lang == TPC_LANG_DE && rec.poiMapName[0] != 0);
    CL::BuildLink b; b.profession = CL::PROF_MESMER; b.specs[2].id = 59;
    CHECK(svc.Resolve(CL::LINK_BUILD, 0, CL::EncodeBuild(b).c_str(), &rec) == TPC_RESOLVED);
    CHECK(std::string(rec.name) == "Illusionist-Build");
    CHECK(svc.Resolve(CL::LINK_BUILD, 0, nullptr, &rec) == TPC_FAILED);
    CHECK(svc.Resolve(CL::LINK_MAP, 4000000, nullptr, &rec) == TPC_FAILED);

    // API data: not ready, then the completion carries the record.
    CHECK(svc.Resolve(CL::LINK_ITEM, 48073, nullptr, &rec) == TPC_NOT_READY);
    CHECK(rec.id == 48073 && rec.status == TPC_NOT_READY);
    Drain(svc.GetResolver(), 1000);
    const auto done = svc.TakeResolved();
    CHECK(done.size() == 1 && done[0].status == TPC_RESOLVED && std::string(done[0].name) == "Zojja-Brustplatte");
    CHECK(done[0].rarity == TPC_RARITY_ASCENDED && done[0].bound == TPC_BOUND_ACCOUNT_ON_ACQUIRE);
    CHECK(svc.Resolve(CL::LINK_ITEM, 48073, nullptr, &rec) == TPC_RESOLVED);

    // The rest of that response was stored, too (no extra request for them).
    TpcRecord many[3];
    for (auto& m : many) m.structSize = sizeof m;
    const uint32_t warmIds[3] = {48073, 19976, 30680};
    CHECK(svc.ResolveMany(CL::LINK_ITEM, warmIds, 3, many) == 3);
    // ResolveMany: one call, cold ids share one request.
    const uint32_t ids[3] = {48073, 111, 222};
    CHECK(svc.ResolveMany(CL::LINK_ITEM, ids, 3, many) == 1);
    CHECK(many[0].status == TPC_RESOLVED && many[1].status == TPC_NOT_READY && many[2].id == 222);
    const int before = http.Count("items?ids=");
    Drain(svc.GetResolver(), 2000);
    CHECK(http.Count("items?ids=111,222") == 1);
    CHECK(http.Count("items?ids=") == before + 1);
    CHECK(svc.TakeResolved().size() == 2);                        // both answered (unknown: failed)
}

static void test_service_events()
{
    FakeHttp http;
    Service svc(http.Fn(), FastOptions());
    CHECK(svc.EventCount() == Events::kEventCount);
    TpcEvent ev; ev.structSize = sizeof ev;
    CHECK(svc.EventById("tequatl-the-sunless", TPC_LANG_EN, &ev));
    CHECK(std::string(ev.id) == "tequatl-the-sunless" && ev.name[0] != 0 && ev.placeCount >= 1);
    CHECK(ev.occurrencesPerDay > 0 && ev.places[0].waypoint[0] == '[');
    CHECK(!svc.EventById("nope", TPC_LANG_EN, &ev));
    TpcOccurrence occ[4];
    CHECK(svc.EventNext("tequatl-the-sunless", 1700000000, occ, 4) == 4);
    CHECK(occ[0].start > 1700000000 && occ[1].start > occ[0].start);
    TpcOccurrence st;
    CHECK(svc.EventState("tequatl-the-sunless", occ[0].start + 1, &st) && st.active == 1);
    TpcEventId ids[2];
    CHECK(svc.EventSearch("", ids, 2) == Events::kEventCount);
    CHECK(ids[0].id[0] != 0);
    CHECK(svc.EventSearch("tequatl", ids, 2) >= 1);
}


// ── Settings page texts, settings.json ──────────────────────────────────────

static std::string Specifiers(const char* s)
{
    std::string out;
    for (const char* p = s; *p; ++p)
        if (*p == '%' && p[1]) { out += p[1]; ++p; }
    return out;
}

static void test_i18n_table()
{
    for (const auto& p : I18n::GermanTable()) {
        CHECK_EQ(Specifiers(p.second), Specifiers(p.first));      // same printf specifiers
        CHECK(std::string(I18n::Tr(p.first, Lang::De)) == p.second);
        CHECK(std::string(I18n::Tr(p.first, Lang::En)) == p.first);
        for (const char* c = p.second; *c; ++c)                   // ASCII + Latin-1 only
            if ((unsigned char)*c >= 0x80) CHECK((unsigned char)*c == 0xC3 || ((unsigned char)*c >= 0x80 && (unsigned char)*c <= 0xBF));
    }
    CHECK(std::string(I18n::Tr("untranslated", Lang::De)) == "untranslated");
}

static void test_settings_json()
{
    CHECK(!Settings::FromJson("").offline);
    CHECK(!Settings::FromJson("garbage").offline);
    Settings s; s.offline = true;
    CHECK(Settings::FromJson(s.ToJson()).offline);
    CHECK(!Settings::FromJson("{\"offline\":\"yes\"}").offline);   // wrong type: default
}

// ── Event data (generated by tools/gen_events.py; moved from Trailpack Memo) ──────────────────────────────

static void test_event_data()
{
    using namespace Trailpack::Core::Events;
    CHECK(kEventCount > 50);
    CHECK(kCategoryCount > 5);

    std::set<std::string> ids;
    for (size_t i = 0; i < kEventCount; ++i) {
        const EventDef& e = kEvents[i];
        CHECK(ids.insert(e.id).second);                  // ids are unique
        CHECK(e.nameEn != nullptr && e.nameEn[0] != '\0');
        CHECK(e.category < kCategoryCount);
        CHECK(e.count > 0);
        for (int k = 0; k < e.count; ++k) {
            CHECK(e.starts[k] < kDayMinutes);
            CHECK(e.durations[k] > 0);
            if (k > 0) CHECK(e.starts[k] > e.starts[k - 1]);   // ascending
        }
        CHECK(e.placeCount >= 1 && e.places != nullptr);
        for (int p = 0; p < e.placeCount; ++p)
            if (e.places[p].waypoint != nullptr) CHECK(std::string(e.places[p].waypoint).rfind("[&B", 0) == 0);
        for (int k = 0; k < e.count; ++k) CHECK(e.placeOf[k] < e.placeCount);
    }

    // Pinned to the wiki schedule: Tequatl at 00:00, 03:00, 07:00, 11:30, 16:00, 19:00 UTC for
    // 30 min; Dragon's Stand every 2 h from 01:30 for 120 min (crosses midnight at 23:30).
    const EventDef* teq = nullptr;
    const EventDef* ds  = nullptr;
    for (size_t i = 0; i < kEventCount; ++i) {
        if (std::string(kEvents[i].id) == "tequatl-the-sunless") teq = &kEvents[i];
        if (std::string(kEvents[i].id) == "dragons-stand")       ds  = &kEvents[i];
    }
    CHECK(teq != nullptr && ds != nullptr);
    if (teq != nullptr) {
        const uint16_t want[] = { 0, 180, 420, 690, 960, 1140 };
        CHECK_EQ((int)teq->count, 6);
        for (int k = 0; k < 6 && k < teq->count; ++k) {
            CHECK_EQ((int)teq->starts[k], (int)want[k]);
            CHECK_EQ((int)teq->durations[k], 30);
        }
    }
    if (ds != nullptr) {
        CHECK_EQ((int)ds->count, 12);
        CHECK_EQ((int)ds->starts[0], 90);
        CHECK_EQ((int)ds->starts[11], 1410);
        CHECK_EQ((int)ds->durations[11], 120);
    }
}

// ── Event schedule maths (EventSchedule.h) ────────────────────────────────────────────

static const int64_t kOct29 = 1793232000;   // 2026-10-29 00:00 UTC

static void test_events_lookup()
{
    using namespace Trailpack::Core::Events;
    CHECK(Find("nope") == nullptr);
    const EventDef* teq = Find("tequatl-the-sunless");
    CHECK(teq != nullptr);
    if (teq == nullptr) return;
    CHECK_EQ(std::string(Name(*teq, Lang::En)), std::string("Tequatl the Sunless"));
    CHECK_EQ(std::string(Name(*teq, Lang::De)), std::string("Tequatl der Sonnenlose"));
    CHECK_EQ(std::string(MapName(teq->places[0], Lang::En)), std::string("Sparkfly Fen"));
    CHECK_EQ(Maps(*teq, Lang::De), std::string("Funkenschwärmersumpf"));
    CHECK_EQ(std::string(WaypointName(teq->places[0], Lang::En)), std::string("Splintered Coast Waypoint"));
    CHECK_EQ(std::string(CategoryName(teq->category, Lang::De)), std::string("Zentraltyria"));
    CHECK_EQ(std::string(CategoryName(255, Lang::En)), std::string(""));

    // No German name: English in both languages; expansion categories stay untranslated.
    const EventDef* storm = Find("dragonstorm");
    CHECK(storm != nullptr);
    if (storm != nullptr) CHECK_EQ(std::string(Name(*storm, Lang::De)), std::string("Dragonstorm"));
    const EventDef* ds = Find("dragons-stand");
    CHECK(ds != nullptr);
    if (ds != nullptr) CHECK_EQ(std::string(CategoryName(ds->category, Lang::De)), std::string("Heart of Thorns"));
}

static void test_events_state()
{
    using namespace Trailpack::Core::Events;
    const EventDef* teq = Find("tequatl-the-sunless");   // 00:00 03:00 07:00 11:30 16:00 19:00, 30 min
    const EventDef* ds  = Find("dragons-stand");         // every 2 h from 01:30, 120 min
    const EventDef* tt  = Find("triple-trouble");        // 01:00 04:00 08:00 12:30 17:00 20:00
    CHECK(teq != nullptr && ds != nullptr && tt != nullptr);
    if (teq == nullptr || ds == nullptr || tt == nullptr) return;

    // Running window edges: [start, start + duration).
    Status s = State(*teq, kOct29);
    CHECK(s.active && s.start == kOct29 && s.end == kOct29 + 1800);
    s = State(*teq, kOct29 + 1799);
    CHECK(s.active && s.start == kOct29);
    s = State(*teq, kOct29 + 1800);
    CHECK(!s.active && s.start == kOct29 + 3 * 3600);

    // Day wrap: one second before midnight, the next start is 00:00 of the next day.
    s = State(*teq, kOct29 - 1);
    CHECK(!s.active && s.start == kOct29);

    // Crossing midnight: Dragon's Stand from 23:30 runs until 01:30, then starts again.
    s = State(*ds, kOct29 + 15 * 60);
    CHECK(s.active && s.start == kOct29 - 1800 && s.end == kOct29 + 5400);
    s = State(*ds, kOct29 + 5400);
    CHECK(s.active && s.start == kOct29 + 5400);

    s = State(*tt, kOct29);
    CHECK(!s.active && s.start == kOct29 + 3600);

    std::vector<Occurrence> next = NextOccurrences(*teq, kOct29 + 3600, 3);
    CHECK_EQ(next.size(), (size_t)3);
    if (next.size() == 3) {
        CHECK_EQ(next[0].start, kOct29 + 3 * 3600);
        CHECK_EQ(next[0].end, kOct29 + 3 * 3600 + 1800);
        CHECK_EQ(next[1].start, kOct29 + 7 * 3600);
        CHECK_EQ(next[2].start, kOct29 + 11 * 3600 + 1800);
    }
    next = NextOccurrences(*teq, kOct29 + 19 * 3600, 2);   // exactly at the last start of the day
    CHECK(next.size() == 2 && next[0].start == kOct29 + 86400 && next[1].start == kOct29 + 86400 + 3 * 3600);
    CHECK(NextOccurrences(*teq, kOct29, 0).empty());
}

// A rotating event is ONE event with several places: the Ley-Line Anomaly every 2 h from
// 00:20 UTC for 20 min, in Timberline Falls, Iron Marches, Gendarran Fields in turn. Its place
// stays the running one until that run has ended, then moves to the next.
static void test_events_rotating_places()
{
    using namespace Trailpack::Core::Events;
    const EventDef* lla = Find("ley-line-anomaly");
    CHECK(lla != nullptr);
    if (lla == nullptr) return;
    CHECK_EQ((int)lla->placeCount, 3);
    CHECK_EQ(Maps(*lla, Lang::En), std::string("Timberline Falls, Iron Marches, Gendarran Fields"));
    CHECK(Find("ley-line-anomaly-timberline-falls") == nullptr);   // no longer three events

    Status s = State(*lla, kOct29 + 10 * 60);                 // 00:10: next run 00:20, Timberline
    CHECK(!s.active && s.start == kOct29 + 20 * 60);
    CHECK_EQ(std::string(MapName(lla->places[s.place], Lang::En)), std::string("Timberline Falls"));
    s = State(*lla, kOct29 + 39 * 60);                        // 00:39: still running in Timberline
    CHECK(s.active && std::string(MapName(lla->places[s.place], Lang::En)) == "Timberline Falls");
    s = State(*lla, kOct29 + 40 * 60);                        // 00:40: ended -> 02:20 Iron Marches
    CHECK(!s.active && s.start == kOct29 + 2 * 3600 + 20 * 60);
    CHECK_EQ(std::string(MapName(lla->places[s.place], Lang::En)), std::string("Iron Marches"));
    CHECK_EQ(std::string(lla->places[s.place].waypoint), std::string("[&BOcBAAA=]"));

    const std::vector<Occurrence> next = NextOccurrences(*lla, kOct29, 3);
    CHECK(next.size() == 3 && next[0].place != next[1].place && next[1].place != next[2].place);

    // Search finds it through any of its maps, in either language.
    bool found = false;
    for (const EventDef* e : Search("eisenmark")) found = found || e == lla;
    CHECK(found);
}

#ifndef _WIN32
// Daylight saving: the maths is UTC only, so a DST time zone gives the same results around
// both 2026 switches (EU: 29 Mar and 25 Oct, 01:00 UTC).
static void test_events_ignore_time_zone()
{
    using namespace Trailpack::Core::Events;
    const EventDef* teq = Find("tequatl-the-sunless");
    CHECK(teq != nullptr);
    if (teq == nullptr) return;
    const int64_t mar29 = kOct29 - 214 * 86400;   // 2026-03-29 00:00 UTC
    const int64_t oct25 = kOct29 - 4 * 86400;     // 2026-10-25 00:00 UTC
    const int64_t probes[] = { mar29 + 3600 - 1, mar29 + 3600, mar29 + 2 * 3600,
                               oct25 + 3600 - 1, oct25 + 3600, oct25 + 2 * 3600 };

    const char* old = std::getenv("TZ");
    const std::string saved = old != nullptr ? old : "";
    std::vector<int64_t> results[2];
    const char* zones[] = { "UTC", "Europe/Berlin" };
    for (int z = 0; z < 2; ++z) {
        setenv("TZ", zones[z], 1);
        tzset();
        for (int64_t p : probes) {
            const Status st = State(*teq, p);
            results[z].push_back(st.start);
            results[z].push_back(NextOccurrences(*teq, p, 1)[0].start);
        }
    }
    if (old != nullptr) setenv("TZ", saved.c_str(), 1); else unsetenv("TZ");
    tzset();

    CHECK(results[0] == results[1]);
    CHECK_EQ(NextOccurrences(*teq, mar29 + 3600, 1)[0].start, mar29 + 3 * 3600);   // 03:00 UTC as always
    CHECK_EQ(NextOccurrences(*teq, oct25 + 3600, 1)[0].start, oct25 + 3 * 3600);
}
#endif

static void test_events_search()
{
    using namespace Trailpack::Core::Events;
    auto has = [](const std::vector<const EventDef*>& v, const char* id) {
        for (const EventDef* e : v) if (std::string(e->id) == id) return true;
        return false;
    };
    CHECK(has(Search("teq"), "tequatl-the-sunless"));
    CHECK(has(Search("SONNENLOSE"), "tequatl-the-sunless"));             // German name, any case
    CHECK(has(Search("FUNKENSCHWÄRMERSUMPF"), "tequatl-the-sunless"));   // German map, umlaut folded
    CHECK(has(Search("dragons-stand"), "dragons-stand"));                // id
    CHECK_EQ(Search("").size(), kEventCount);
    CHECK(Search("zzzz-no-such-event").empty());
}

// ── Events ──────────────────────────────────────────────────────────────────

static void test_events_schedule_and_search()
{
    const Events::EventDef* teq = Events::Find("tequatl-the-sunless");
    CHECK(teq != nullptr);
    if (!teq) return;
    CHECK(std::string(Events::Name(*teq, Lang::De)).size() > 0);
    // Occurrences are ascending and strictly after `now`.
    const int64_t now = 1700000000;
    const auto next = Events::NextOccurrences(*teq, now, 5);
    CHECK(next.size() == 5);
    for (size_t i = 0; i < next.size(); ++i) {
        CHECK(next[i].start > now);
        if (i) CHECK(next[i].start > next[i - 1].start);
    }
    const Events::Status st = Events::State(*teq, next[0].start + 1);
    CHECK(st.active && st.start == next[0].start);
    CHECK(!Events::State(*teq, next[0].start - 1).active);
    // Search: case-insensitive, German umlauts, empty query lists all.
    CHECK(Events::Search("").size() == Events::kEventCount);
    CHECK(!Events::Search("TEQUATL").empty());
    CHECK(Events::Find("no-such-event") == nullptr);
    CHECK(std::string(Events::CategoryShort(teq->category, Lang::De)).size() > 0);
    // Category labels (moved from Memo): abbreviations stay, generic names are translated,
    // an unknown index is "".
    for (uint8_t i = 0; i < Events::kCategoryCount; ++i) {
        CHECK(*Events::CategoryShort(i, Lang::En) != '\0');
        CHECK(*Events::CategoryShort(i, Lang::De) != '\0');
    }
    CHECK_EQ(std::string(Events::CategoryShort(0, Lang::De)), std::string("Zentral"));
    CHECK_EQ(std::string(Events::CategoryShort(uint8_t(Events::kCategoryCount), Lang::En)), std::string(""));
}

int main()
{
    test_offline_build_label();
    test_offline_waypoint();
    test_item_armor_tooltip();
    test_item_weapon_tooltip();
    test_item_rune_bonuses();
    test_item_nosell_tradeable();
    test_item_rarity_values();
    test_item_subtype_wording();
    test_item_german();
    test_text_helpers();
    test_skill_parse();
    test_wiki_fallback_parse();
    test_wiki_url_context();
    test_resolver_skill_fallback_paths();
    test_resolver_wiki_maintenance_fails_then_retries();
    test_trait_parse();
    test_recipe_resolve();
    test_recipe_localized();
    test_skin_parse_and_lang();
    test_cache_roundtrip();
    test_language_namespacing();
    test_invalid_id_and_type();
    test_terminal_outcome_invariant();
    test_failure_cooldown();
    test_batching_and_window();
    test_stale_refresh_on_new_build();
    test_offline_mode();
    test_teardown_awaits_inflight_fetch();
    test_token_bucket();
    test_resolver_respects_budget();
    test_events_schedule_and_search();
    test_event_data();
    test_events_lookup();
    test_events_state();
    test_events_rotating_places();
#ifndef _WIN32
    test_events_ignore_time_zone();
#endif
    test_events_search();
    test_abi_layout();
    test_abi_fill_respects_struct_size();
    test_abi_fill_caps();
    test_service_end_to_end();
    test_service_events();
    test_i18n_table();
    test_settings_json();

    if (s_failures == 0) {
        std::cout << "ALL PASS\n";
        return 0;
    }
    std::cerr << s_failures << " test(s) FAILED\n";
    return 1;
}
