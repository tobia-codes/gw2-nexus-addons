#pragma once
// Reset.h — PURE Guild Wars 2 reset times (no ImGui/Nexus).
//
// GW2 resets at fixed UTC times, the same for every region:
//   daily  -- every day at 00:00 UTC
//   weekly -- every Monday at 07:30 UTC
// All times are Unix seconds (UTC), e.g. from std::time(nullptr).

#include <cstdint>
#include <string>

namespace Magpie { namespace Reset {

// The most recent reset at or before `now`.
int64_t LastDaily(int64_t now);
int64_t LastWeekly(int64_t now);

// The next reset strictly after `now`.
int64_t NextDaily(int64_t now);
int64_t NextWeekly(int64_t now);

// Compact countdown for `seconds` (>= 0): "2d 5h", "3h 12m", "12m", "<1m".
std::string FormatCountdown(int64_t seconds);

}} // namespace Magpie::Reset
