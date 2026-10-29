#pragma once
// EventChip — event timer chips "{{event:<id>}}" in notes (NON-PURE: ImGui/Win32).
//
// View / preview: "<name> - in 23 Min" or "<name> - seit 4 Min" (live), a tooltip with the
// next starts in local time, left click copies the waypoint chat code ("Waypoint copied" at
// the mouse), right click offers Copy waypoint / Open in wiki. The editor shows the name only.
// Rotating events (several places) use the place of the running occurrence, else the next.
// Unknown ids render as a grey "Unknown event (<id>)" chip. Schedule maths: Events.h.

#include "imgui.h"
#include <string>
#include "ChipDone.h"

namespace Magpie {

// Editor label (name only, current language) and colour for an event token "{{event:<id>}}".
std::string EventChipLabel(const std::string& token);
ImU32       EventChipColor(const std::string& token);

// Width a view chip for event `id` takes at `fontSize` now; matches DrawEventChip.
float MeasureEventChip(ImFont* font, float fontSize, const std::string& id,
                       const ChipDoneStyle* done = nullptr);

// Draw a view chip for event `id` at screen `pos`; returns its width. Registers the tooltip,
// the left-click copy and the right-click menu; `uid` makes the menu's popup id unique.
// In a checked task (`done`): name only in the task's grey, or with the countdown in a much
// lighter event colour; tooltip and menu only if `done->popups`, no left-click copy.
float DrawEventChip(ImDrawList* dl, ImFont* font, float fontSize, const ImVec2& pos,
                    const std::string& id, int uid, const ChipDoneStyle* done = nullptr);

} // namespace Magpie
