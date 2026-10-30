#include "chips/ChipColor.h"
#include "chatlinks/ChatLinks.h"   // ChatLinks::LINK_ITEM
#include "TrailpackCoreApi.h"   // TpcRarity (TPC_RARITY_*)

namespace Trailpack {

// IM_COL32(r, g, b, 255) packed (ABGR, alpha 255).
static constexpr uint32_t Pack(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(0xFFu) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
}

// ── In-game chat link colours ─────────────────────────────────────────────────
// Default link tint — waypoints, skills, builds, skins all use this blue.
static constexpr uint32_t kLinkBlue = Pack(120, 200, 255);

// Per-rarity item palette (0-1 values converted to 0-255, ImGui rounding).
//   Junk (0.67,0.67,0.67)  Basic (1,1,1)         Fine (0.38,0.64,0.85)
//   Masterwork (0.12,0.84,0) Rare (1,0.84,0)      Exotic (1,0.67,0)
//   Ascended (0.98,0.24,0.55) Legendary (0.64,0.21,0.93)
static constexpr uint32_t kRarJunk       = Pack(171, 171, 171);
static constexpr uint32_t kRarBasic      = Pack(255, 255, 255);
static constexpr uint32_t kRarFine       = Pack( 97, 163, 217);
static constexpr uint32_t kRarMasterwork = Pack( 31, 214,   0);
static constexpr uint32_t kRarRare       = Pack(255, 214,   0);
static constexpr uint32_t kRarExotic     = Pack(255, 171,   0);
static constexpr uint32_t kRarAscended   = Pack(250,  61, 140);
static constexpr uint32_t kRarLegendary  = Pack(163,  54, 237);

uint32_t ChipColorForItemRarity(uint8_t rarity) {
    switch (rarity) {
        case TPC_RARITY_JUNK:          return kRarJunk;
        case TPC_RARITY_BASIC:         return kRarBasic;
        case TPC_RARITY_FINE:          return kRarFine;
        case TPC_RARITY_MASTERWORK:    return kRarMasterwork;
        case TPC_RARITY_RARE:          return kRarRare;
        case TPC_RARITY_EXOTIC:        return kRarExotic;
        case TPC_RARITY_ASCENDED:      return kRarAscended;
        case TPC_RARITY_LEGENDARY:     return kRarLegendary;
        case TPC_RARITY_UNKNOWN:
        default:               return kRarBasic;   // unresolved / absent -> Basic (white)
    }
}

uint32_t ChipColorForType(uint8_t /*linkType*/) {
    // Every non-item chip type uses the one default link blue.
    return kLinkBlue;
}

uint32_t ChipColor(uint8_t linkType, uint8_t rarity) {
    if (linkType == ChatLinks::LINK_ITEM)
        return ChipColorForItemRarity(rarity);
    return ChipColorForType(linkType);
}

} // namespace Trailpack
