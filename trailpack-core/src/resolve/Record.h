#pragma once
// Record.h — PURE internal record of one resolved link (any type). The ABI record
// (public/TrailpackCoreApi.h) is filled from it at the boundary (resolve/Fill).

#include <cstdint>
#include <string>
#include <vector>

#include "core/Lang.h"

namespace Trailpack { namespace Core {

enum class Rarity : uint8_t {
    Unknown = 0, Junk, Basic, Fine, Masterwork, Rare, Exotic, Ascended, Legendary,
};

enum class Bound : uint8_t {
    None = 0, AccountOnAcquire, SoulOnAcquire, AccountOnUse, SoulOnUse,
};

struct Fact {
    std::string icon;   // render-service URL, "" if none
    std::string text;   // pre-formatted line, e.g. "Range: 1200"
};

struct Record {
    uint8_t     type = 0;      // chat-link type byte
    uint32_t    id   = 0;
    Lang        lang = Lang::En;

    std::string name;
    std::string icon;          // render-service URL
    std::string description;   // plain text (game markup stripped), lines separated by '\n'
    std::vector<Fact> facts;   // tooltip lines

    // Item
    Rarity   rarity      = Rarity::Unknown;
    Bound    bound       = Bound::None;
    bool     noSell      = false;   // cannot be sold to a vendor (does NOT imply untradeable)
    bool     tradeable   = false;   // eligible for the trading post
    int32_t  vendorValue = 0;       // copper

    // Recipe
    uint32_t outputItemId = 0;
    uint16_t outputCount  = 0;
    uint16_t minRating    = 0;

    // Waypoint / PoI
    std::string mapName;
    uint8_t     poiKind = 0;        // GameData::PoiKind
};

}} // namespace Trailpack::Core
