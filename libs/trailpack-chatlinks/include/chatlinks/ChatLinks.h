#pragma once
// ============================================================================
// ChatLinks — GW2 chat-link codec shared by every Trailpack addon.
// ----------------------------------------------------------------------------
// A chat link is "[&" + base64 + "]". The first decoded byte is the link type,
// the rest is a type-specific little-endian payload. This unit only handles
// structure (type, ids, spans); turning ids into names / icons is Trailpack
// Core's job.
//
// PURE: C++ standard library only, no global mutable state — safe from any
// thread and in host tests.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Trailpack { namespace ChatLinks {

// First payload byte.
enum LinkType : uint8_t {
    LINK_NONE          = 0x00,
    LINK_COIN          = 0x01,
    LINK_ITEM          = 0x02,
    LINK_NPC_TEXT      = 0x03,
    LINK_MAP           = 0x04,  // waypoint / point of interest / vista
    LINK_PVP_GAME      = 0x05,
    LINK_SKILL         = 0x06,
    LINK_TRAIT         = 0x07,
    LINK_USER          = 0x08,
    LINK_RECIPE        = 0x09,
    LINK_SKIN          = 0x0A,
    LINK_OUTFIT        = 0x0B,
    LINK_WVW_OBJECTIVE = 0x0C,
    LINK_BUILD         = 0x0D,
    LINK_ACHIEVEMENT   = 0x0E,
    LINK_WARDROBE      = 0x0F,
};

// Flags in the high byte of an item link's id word.
enum ItemFlags : uint8_t {
    ITEM_HAS_SKIN     = 0x80,
    ITEM_HAS_UPGRADE1 = 0x40,
    ITEM_HAS_UPGRADE2 = 0x20,
};

// Profession codes of build links (1..9).
enum Profession : uint8_t {
    PROF_GUARDIAN = 1, PROF_WARRIOR, PROF_ENGINEER, PROF_RANGER, PROF_THIEF,
    PROF_ELEMENTALIST, PROF_MESMER, PROF_NECROMANCER, PROF_REVENANT,
};

struct ItemLink {
    uint32_t itemId   = 0;
    int      count    = 1;
    uint32_t skinId   = 0;   // 0 = none
    uint32_t upgrade1 = 0;   // 0 = none
    uint32_t upgrade2 = 0;
};

struct BuildLink {
    uint8_t profession = 0;
    struct Spec {
        uint8_t id = 0;              // specialization id, 0 = empty slot
        uint8_t traits[3] = {0, 0, 0}; // adept / master / grandmaster: 0 none, 1 top, 2 middle, 3 bottom
    } specs[3];
    uint16_t landSkills[5]  = {0};   // heal, utility 1-3, elite (palette ids)
    uint16_t waterSkills[5] = {0};
    uint8_t  professionData[16] = {0}; // ranger pets / revenant legends + inactive skills, raw
    std::vector<uint16_t> weapons;     // optional extension (weapon types)
    std::vector<uint32_t> skillOverrides;
};

// A run of a text line: plain text or one chat link.
struct Segment {
    bool        isLink = false;
    std::string raw;                 // exact source slice
    LinkType    type = LINK_NONE;    // links only
    uint32_t    id = 0;              // item / skin / skill / trait / recipe / PoI / ... id
    uint32_t    secondId = 0;        // WvW objective: map id
    int         count = 1;           // item quantity
};

// --- Base64 (standard alphabet, '=' padding) ---
std::string          Base64Encode(const uint8_t* data, size_t len);
std::vector<uint8_t> Base64Decode(const std::string& text);   // empty on any invalid character

// Payload bytes of "[&...]"; empty when `link` is not exactly one chat link.
std::vector<uint8_t> Payload(const std::string& link);
LinkType             TypeOf(const std::string& link);         // LINK_NONE when not a link

// --- Decode / encode ---
bool DecodeItem(const std::string& link, ItemLink& out);
bool DecodeBuild(const std::string& link, BuildLink& out);

std::string EncodeItem(uint32_t itemId, int count = 1, uint32_t skinId = 0,
                       uint32_t upgrade1 = 0, uint32_t upgrade2 = 0);   // count clamped to 1..250
std::string EncodeId(LinkType type, uint32_t id);   // skill, trait, skin, recipe, map, outfit, achievement
std::string EncodeBuild(const BuildLink& build);

// --- Labels (English, used when nothing better is known) ---
const char* TypeLabel(LinkType type);         // "[Item]", "[Waypoint]", ...
const char* ProfessionName(uint8_t code);     // "Mesmer"; "" when unknown

// Split a UTF-8 line into plain runs and chat links, in order and without gaps:
// concatenating every `raw` gives the input back. A link is "[&" + base64 + "]"
// whose payload decodes to at least the type byte; anything else stays plain.
std::vector<Segment> Split(const std::string& utf8);

}} // namespace Trailpack::ChatLinks
