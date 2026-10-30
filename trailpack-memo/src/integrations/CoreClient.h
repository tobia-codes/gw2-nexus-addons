#pragma once
// CoreClient — the ONLY place that talks to Trailpack Core (NON-PURE: Nexus). Core is required
// for chat-link names / icons / tooltips and for event timers; without it Memo keeps working as
// a notepad and shows those features greyed out (Core PLAN D17).
//
// Lifetime contract (public/TrailpackCoreApi.h): the TpcApi pointer is re-resolved before
// every use and never kept; READY / UNLOADING / RESOLVED are subscribed, PING raised at load;
// on UNLOADING every cached answer is dropped.

#include <cstdint>
#include <string>
#include <vector>

#include "nexus/Nexus.h"
#include "TrailpackCoreApi.h"

namespace Trailpack { namespace CoreClient {

// Lowest Core ABI Memo understands. Core only appends fields, so any newer one works.
constexpr uint32_t kMinApiVersion = 1u;

void Init(AddonAPI_t* api);    // AddonLoad, after APIDefs is set
void Shutdown();               // AddonUnload, before APIDefs is cleared

enum class State { Absent, TooOld, Ready };
State Status();                // live, never latched
inline bool Present() { return Status() == State::Ready; }

// Chat links. True with a resolved record in `out`; false while Core is absent, the answer is
// on its way (a later frame has it) or it failed. `chatCode` is needed for build links.
bool Resolve(uint8_t linkType, uint32_t id, const char* chatCode, TpcRecord& out);
// Ask for an item without waiting (e.g. the rune / sigil of an item chip, D18).
void Prefetch(uint32_t itemId);

// Once per frame on the render thread, before any event call: (re)loads the event list when
// Core appeared / went away or Memo's language changed.
void Frame();

// ── Events (texts in Memo's UI language, cached until Core or the language changes) ──
struct EventPlace {
    std::string map;            // "" if unknown
    std::string waypoint;       // chat code, "" if none
    std::string waypointName;
};
struct Event {
    std::string id, name, category, categoryShort, wikiPage, maps;
    std::vector<EventPlace> places;
};
struct Occurrence {
    int64_t start = 0;
    int64_t end   = 0;
    uint8_t place = 0;
    bool    active = false;
};

// All events in data order (empty without Core); nullptr for an unknown id.
const std::vector<Event>& Events();
const Event* FindEvent(const std::string& id);
// The running occurrence at `now`, else the next one.
bool EventState(const std::string& id, int64_t now, Occurrence& out);
std::vector<Occurrence> EventNext(const std::string& id, int64_t now, int n);
// Matching events in data order ("" = all).
std::vector<const Event*> SearchEvents(const std::string& query);

}} // namespace Trailpack::CoreClient
