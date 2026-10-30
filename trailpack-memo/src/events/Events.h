#pragma once
// Events.h — PURE editor side of event timers (no ImGui/Nexus): the note syntax, the "/e "
// trigger and time texts. Event data, schedules and search come from Trailpack Core
// (integrations/CoreClient, Core PLAN D14). Times are Unix seconds (UTC).

#include <cstdint>
#include <string>
#include <vector>
#include "core/I18n.h"

namespace Trailpack { namespace Events {

// ── Note syntax {{event:<id>}} (D7) ─────────────────────────────────────────
// An id is 1..64 of a-z, 0-9 and '-'. Unknown ids are still tokens (shown as "unknown").
std::string Token(const std::string& id);
// The first token in `s` at or after `from`: returns its offset and sets `length` and `id`;
// std::string::npos if there is none.
size_t FindToken(const std::string& s, size_t from, size_t& length, std::string& id);
// True if all of `s` is one token (sets `id` when given).
bool IsToken(const std::string& s, std::string* id = nullptr);

// ── Inline suggestions in the editor (D9) ───────────────────────────────────
// "/e " or "/event " (any case) at the start of a line or after a space, then the query up to
// the caret: same line, no chip cell (U+FFFC), at most kMaxTriggerQuery codepoints. `text` is
// the editor's text as one codepoint per cell.
constexpr int kMaxTriggerQuery = 40;
struct Trigger {
    bool        found = false;
    int         start = 0;    // cell of the '/'; the cells [start, caret) are replaced by the chip
    std::string query;        // UTF-8, may be empty ("/e " alone lists every event)
};
Trigger FindTrigger(const std::vector<unsigned int>& text, int caret);

// Fallback name for an id while Trailpack Core is absent: "tequatl-the-sunless" ->
// "Tequatl the sunless".
std::string PrettyId(const std::string& id);

// Chip time text in the current language (I18n::Tr): before the start "in 45 s", "in 23 min",
// "in 1 h 23 min", "in 2 h"; while running "for 4 min" (DE: "in 23 Min", "seit 4 Min").
// `seconds` = time until the start, or since the start when `active`.
std::string FormatTime(int64_t seconds, bool active);
// A plain duration in the current language: "30 min", "2 h", "1 h 30 min" (DE: "30 Min", ...).
std::string FormatDuration(int64_t seconds);

}} // namespace Trailpack::Events
