#pragma once
// ChipDone.h — how chips inside a checked task are drawn (Options > "Events"; the two options
// apply to event chips, chat-link chips are always plain grey text there).
// Passed to DrawRichChip / DrawEventChip (and their Measure functions); nullptr = open task.

#include "imgui.h"

namespace Magpie {

struct ChipDoneStyle {
    bool  showTime = false;   // event chips keep their countdown, in a much lighter event colour
    bool  popups   = false;   // event chips keep tooltip + right-click menu; never a left-click action
    ImU32 textCol  = 0;       // the task's dimmed text colour (label when not showing the time)
};

} // namespace Magpie
