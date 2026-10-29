#pragma once
// Events.h — PURE event timer lookup, schedule maths and texts (no ImGui/Nexus).
//
// Data: events/EventData.h (generated, see plans/EVENTS.md). All times are Unix seconds
// (UTC), e.g. from std::time(nullptr). Nothing here uses local time or a time zone: GW2
// schedules are fixed in UTC, so a daylight-saving switch never changes a countdown. Local
// clock times (tooltips) are the caller's job, converted per start.

#include <cstdint>
#include <string>
#include <vector>
#include "I18n.h"
#include "events/EventData.h"

namespace Magpie { namespace Events {

// The event with this id, or nullptr (unknown id: e.g. removed from the data).
const EventDef* Find(const std::string& id);

// ── Note syntax {{event:<id>}} (D7) ─────────────────────────────────────────
// An id is 1..64 of a-z, 0-9 and '-'. Unknown ids are still tokens (shown as "unknown").
std::string Token(const std::string& id);
// The first token in `s` at or after `from`: returns its offset and sets `length` and `id`;
// std::string::npos if there is none.
size_t FindToken(const std::string& s, size_t from, size_t& length, std::string& id);
// True if all of `s` is one token (sets `id` when given).
bool IsToken(const std::string& s, std::string* id = nullptr);

// Where an event stands at `now`.
struct Status {
    bool    active = false;   // an occurrence is running (start <= now < end)
    int64_t start  = 0;       // active: start of the running occurrence; else the next start
    int64_t end    = 0;       // end of that occurrence
};
Status State(const EventDef& e, int64_t now);

// The next `n` starts strictly after `now`, ascending.
std::vector<int64_t> NextStarts(const EventDef& e, int64_t now, int n);

// Name / map / category in `lang` (German falls back to English; map may be "").
const char* Name(const EventDef& e, I18n::Lang lang);
const char* Map(const EventDef& e, I18n::Lang lang);
const char* CategoryName(uint8_t category, I18n::Lang lang);

// Events matching `query` in data order: case-insensitive substring of the name, map or id in
// either language (German umlauts included). An empty query matches every event.
std::vector<const EventDef*> Search(const std::string& query);

// Chip time text in the current language (I18n::Tr): before the start "in 45 s", "in 23 min",
// "in 1 h 23 min", "in 2 h"; while running "for 4 min" (DE: "in 23 Min", "seit 4 Min").
// `seconds` = time until the start, or since the start when `active`.
std::string FormatTime(int64_t seconds, bool active);
// A plain duration in the current language: "30 min", "2 h", "1 h 30 min" (DE: "30 Min", ...).
std::string FormatDuration(int64_t seconds);

}} // namespace Magpie::Events
