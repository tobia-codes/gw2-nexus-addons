#pragma once
// EventSchedule.h — PURE event lookup, schedule maths, texts and search over the built-in
// event data (generated/EventData, PLAN D13 / D14).
//
// All times are Unix seconds (UTC). GW2 schedules are fixed in UTC, so nothing here uses a
// time zone. Consumers (e.g. Trailpack Memo) keep their own note syntax and time formatting.

#include <cstdint>
#include <string>
#include <vector>

#include "core/Lang.h"
#include "generated/EventData.h"

namespace Trailpack { namespace Core { namespace Events {

// The event with this id, or nullptr (unknown id: e.g. removed from the data).
const EventDef* Find(const std::string& id);

// One occurrence of an event: when and where (index into EventDef::places).
struct Occurrence {
    int64_t start = 0;
    int64_t end   = 0;
    uint8_t place = 0;
};

// Where an event stands at `now`: the running occurrence, else the next one. A rotating event
// shows its current place while it runs and switches to the next place only after it ended.
struct Status : Occurrence {
    bool active = false;      // the occurrence is running (start <= now < end)
};
Status State(const EventDef& e, int64_t now);

// The next `n` occurrences starting strictly after `now`, ascending.
std::vector<Occurrence> NextOccurrences(const EventDef& e, int64_t now, int n);

// Texts in `lang` (German falls back to English; map / waypoint name may be "").
const char* Name(const EventDef& e, Lang lang);
const char* CategoryName(uint8_t category, Lang lang);
// Short category label: expansion abbreviations ("HoT", "LW3") stay as they are, generic
// names are translated ("Core" / "Zentral"); else the full name.
const char* CategoryShort(uint8_t category, Lang lang);
const char* MapName(const PlaceDef& p, Lang lang);
const char* WaypointName(const PlaceDef& p, Lang lang);
// All maps of an event, ", "-separated, without repeats ("Timberline Falls, Iron Marches").
std::string Maps(const EventDef& e, Lang lang);

// Events matching `query` in data order: case-insensitive substring of the name, any map or
// the id in either language (German umlauts included). An empty query matches every event.
std::vector<const EventDef*> Search(const std::string& query);

}}} // namespace Trailpack::Core::Events
