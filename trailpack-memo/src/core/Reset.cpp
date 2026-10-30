// Reset.cpp — see Reset.h.
#include "core/Reset.h"

namespace Trailpack { namespace Reset {

static const int64_t kDay        = 24 * 60 * 60;
static const int64_t kWeek       = 7 * kDay;
static const int64_t kWeeklyTime = 7 * 60 * 60 + 30 * 60;   // Monday 07:30 UTC

// Floor division (correct for negative values, too).
static int64_t floorDiv(int64_t a, int64_t b)
{
    return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0);
}

int64_t LastDaily(int64_t now)
{
    return floorDiv(now, kDay) * kDay;
}

int64_t NextDaily(int64_t now)
{
    return LastDaily(now) + kDay;
}

int64_t LastWeekly(int64_t now)
{
    // 1970-01-01 was a Thursday, so the first Monday 07:30 UTC is 4 days + 07:30 later
    // (1970-01-05). Weekly resets are that instant plus whole weeks.
    const int64_t anchor = 4 * kDay + kWeeklyTime;
    return anchor + floorDiv(now - anchor, kWeek) * kWeek;
}

int64_t NextWeekly(int64_t now)
{
    return LastWeekly(now) + kWeek;
}

std::string FormatCountdown(int64_t seconds)
{
    if (seconds < 60) return "<1m";
    const int64_t d = seconds / kDay;
    const int64_t h = (seconds % kDay) / 3600;
    const int64_t m = (seconds % 3600) / 60;
    if (d > 0) return std::to_string(d) + "d " + std::to_string(h) + "h";
    if (h > 0) return std::to_string(h) + "h " + std::to_string(m) + "m";
    return std::to_string(m) + "m";
}

}} // namespace Trailpack::Reset
