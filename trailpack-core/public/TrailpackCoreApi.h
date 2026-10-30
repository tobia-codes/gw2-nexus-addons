// ============================================================================
// Trailpack Core — public ABI for consumer addons (PLAN D5-D8).
// ----------------------------------------------------------------------------
// Pure: <cstdint> only — no Nexus, no Win32, no STL across the boundary.
//
// Getting the API: re-resolve it before EVERY use, never keep the pointer:
//
//   auto* api = (const TpcApi*)nexus->DataLink_Get(TRAILPACK_CORE_DATALINK);
//   if (!api || api->apiVersion < MY_MIN_VERSION || !api->Resolve) -> Core absent / too old
//
// Lifetime:
//   * Subscribe EV_TRAILPACK_CORE_READY / _UNLOADING / _RESOLVED, then raise
//     EV_TRAILPACK_CORE_PING (Core answers with READY if it loaded first).
//   * On UNLOADING drop everything you got from Core. The DataLink block outlives the
//     unload, but its function pointers are zeroed first — calling one later crashes.
//   * Core absent is a normal state: the consumer degrades, it never crashes.
//
// Answers never contain pointers into Core memory (D7): everything is copied into structs
// the caller owns. Every struct starts with `structSize`: the caller sets it to its own
// sizeof(...) before a call, Core fills at most that many bytes and writes back how many it
// filled. Fields are only ever appended (D6), so an older consumer keeps working with a
// newer Core and the other way round.
//
// Resolving (D8):
//   * Built-in data (waypoints / PoIs, build links, events) answers synchronously.
//   * API data (item, skin, skill, trait, recipe): warm -> TPC_RESOLVED at once; cold ->
//     TPC_NOT_READY, fetched in the background, then EV_TRAILPACK_CORE_RESOLVED with a
//     TpcRecord* payload (valid only during the handler — copy it). TPC_FAILED: unknown id,
//     offline, or a failure in cooldown (retry later).
//   * Names come in Core's active language (German or English, D12).
// ============================================================================
#pragma once
#include <cstdint>

#define TRAILPACK_CORE_API_VERSION   1u
#define TRAILPACK_CORE_DATALINK      "TRAILPACK_CORE_API"
#define EV_TRAILPACK_CORE_READY      "EV_TRAILPACK_CORE_READY"      // Core is live (load + ping reply)
#define EV_TRAILPACK_CORE_UNLOADING  "EV_TRAILPACK_CORE_UNLOADING"  // Core goes away: drop everything
#define EV_TRAILPACK_CORE_PING       "EV_TRAILPACK_CORE_PING"       // ask Core to announce READY
#define EV_TRAILPACK_CORE_RESOLVED   "EV_TRAILPACK_CORE_RESOLVED"   // payload: const TpcRecord*

enum TpcStatus : uint8_t {
    TPC_NOT_READY = 0,   // fetch queued; EV_TRAILPACK_CORE_RESOLVED follows
    TPC_RESOLVED  = 1,
    TPC_FAILED    = 2,
};

enum TpcLang : uint8_t { TPC_LANG_EN = 0, TPC_LANG_DE = 1 };

// Item rarity; TPC_RARITY_UNKNOWN is "not known", never a real tier.
enum TpcRarity : uint8_t {
    TPC_RARITY_UNKNOWN = 0, TPC_RARITY_JUNK, TPC_RARITY_BASIC, TPC_RARITY_FINE, TPC_RARITY_MASTERWORK,
    TPC_RARITY_RARE, TPC_RARITY_EXOTIC, TPC_RARITY_ASCENDED, TPC_RARITY_LEGENDARY,
};

enum TpcBound : uint8_t {
    TPC_BOUND_NONE = 0, TPC_BOUND_ACCOUNT_ON_ACQUIRE, TPC_BOUND_SOUL_ON_ACQUIRE,
    TPC_BOUND_ACCOUNT_ON_USE, TPC_BOUND_SOUL_ON_USE,
};

enum TpcPoiKind : uint8_t { TPC_POI_WAYPOINT = 0, TPC_POI_LANDMARK = 1, TPC_POI_VISTA = 2 };

// One tooltip line ("Range: 1200", "+101 Ferocity", "3 Glob of Ectoplasm").
struct TpcFact {
    char icon[128];    // render-service URL, "" if none (download it yourself)
    char text[160];
};

#define TPC_MAX_FACTS 16

// One resolved link. Strings are UTF-8, NUL-terminated, cut at a character boundary.
struct TpcRecord {
    uint32_t structSize;      // in: your sizeof(TpcRecord); out: bytes filled. FIRST FIELD.
    uint8_t  linkType;        // chat-link type byte (0x02 item, 0x04 PoI, 0x06 skill, ...)
    uint8_t  status;          // TpcStatus
    uint8_t  lang;            // TpcLang of the texts
    uint8_t  _pad0;
    uint32_t id;              // item / skin / skill / trait / recipe / PoI id (0 for builds)

    char     name[128];       // display name; "Mirage Build" for builds, "Recipe: ..." for recipes
    char     iconUrl[256];    // render-service URL, "" if none
    char     description[512];// plain text, lines separated by '\n'
    uint8_t  factCount;
    uint8_t  _pad1[3];
    TpcFact  facts[TPC_MAX_FACTS];

    // Item (0x02); recipes carry the output's rarity, skins their own
    uint8_t  rarity;          // TpcRarity
    uint8_t  bound;           // TpcBound
    uint8_t  noSell;          // 1 = cannot be sold to a vendor (does NOT mean untradeable)
    uint8_t  tradeable;       // 1 = may be listed on the trading post
    int32_t  vendorValue;     // copper

    // Recipe (0x09)
    uint32_t recipeOutputItemId;   // resolve as an item link for the full output tooltip
    uint16_t recipeOutputCount;
    uint16_t recipeMinRating;

    // Waypoint / PoI (0x04)
    char     poiMapName[96];
    uint8_t  poiKind;         // TpcPoiKind
    uint8_t  _pad2[3];
};

// ── Events (built in, D13 / D14) ─────────────────────────────────────────────
// Times are Unix seconds (UTC). Every event repeats daily from 00:00 UTC.

#define TPC_MAX_EVENT_PLACES 8

struct TpcEventPlace {
    char map[96];              // "" if unknown
    char waypoint[48];         // chat code "[&B...]", "" if none
    char waypointName[96];
};

struct TpcEvent {
    uint32_t structSize;       // FIRST FIELD (see the header comment)
    char     id[64];           // stable English slug
    char     name[128];
    char     category[64];
    char     categoryShort[24];// "HoT", "LW3", "Core" / "Zentral", ...
    char     wikiPage[160];    // English wiki page (may contain "#section")
    char     maps[192];        // all maps, ", "-separated
    uint8_t  placeCount;
    uint8_t  occurrencesPerDay;
    uint8_t  _pad[2];
    TpcEventPlace places[TPC_MAX_EVENT_PLACES];
};

struct TpcOccurrence {
    int64_t start;
    int64_t end;
    uint8_t place;             // index into TpcEvent::places
    uint8_t active;            // 1 = running at the asked time (EventState only)
    uint8_t _pad[6];
};

struct TpcEventId {
    char id[64];
};

// ── Function table (DataLink TRAILPACK_CORE_DATALINK) ────────────────────────
struct TpcApi {
    uint32_t apiVersion;       // TRAILPACK_CORE_API_VERSION. FIRST FIELD.
    uint32_t structSize;       // sizeof(TpcApi) of the running Core: functions are only appended

    // Resolve one link. `chatCode` ("[&...]") is required for build links (they carry no id)
    // and ignored otherwise. Set out->structSize first.
    TpcStatus (*Resolve)(uint8_t linkType, uint32_t id, const char* chatCode, TpcRecord* out);

    // Resolve a list of ids of one type in one call: out[i] answers ids[i]; every out[i] must
    // have structSize set (all equal). Cold ids are queued together, so they share requests.
    // Returns how many are TPC_RESOLVED right now.
    uint32_t (*ResolveMany)(uint8_t linkType, const uint32_t* ids, uint32_t count, TpcRecord* out);

    // Core's language for names (TpcLang): follows the Nexus language, else English.
    uint8_t (*ActiveLanguage)();

    // Events, texts in `lang` (TpcLang). False / 0 for an unknown id.
    uint32_t (*EventCount)();
    bool     (*EventByIndex)(uint32_t index, uint8_t lang, TpcEvent* out);
    bool     (*EventById)(const char* id, uint8_t lang, TpcEvent* out);
    // The running occurrence at `now`, else the next one (active = 0).
    bool     (*EventState)(const char* id, int64_t now, TpcOccurrence* out);
    // Up to `max` occurrences starting after `now`, ascending; returns how many were written.
    uint32_t (*EventNext)(const char* id, int64_t now, TpcOccurrence* out, uint32_t max);
    // Events matching `query` (name, map or id, either language, case-insensitive; "" = all),
    // in data order. Writes up to `max` ids, returns the total number of matches.
    uint32_t (*EventSearch)(const char* query, TpcEventId* out, uint32_t max);
};
