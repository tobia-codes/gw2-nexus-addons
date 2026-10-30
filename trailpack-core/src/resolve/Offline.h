#pragma once
// Offline.h — PURE answers from the built-in game data (generated/GameData): waypoint / PoI
// links and build links, synchronously and without network (PLAN D8 / D13).

#include <cstdint>
#include <string>

#include "resolve/Record.h"

namespace Trailpack { namespace Core { namespace Offline {

// True for the link types answered here (map / PoI, build).
bool Serves(uint8_t type);

// PoI id -> name, map name, kind. False if the id is unknown.
bool Poi(uint32_t id, Lang lang, Record& out);

// Build link (full "[&...]" code) -> "<elite spec or profession> Build" ("Mirage Build",
// German "Illusionist-Build"). False if the code is not a build link.
bool Build(const std::string& chatCode, Lang lang, Record& out);

// Specialisation / profession names (nullptr / "" when unknown).
const char* SpecName(uint16_t specId, Lang lang);
bool        SpecIsElite(uint16_t specId);
const char* ProfessionName(uint8_t code, Lang lang);

}}} // namespace Trailpack::Core::Offline
