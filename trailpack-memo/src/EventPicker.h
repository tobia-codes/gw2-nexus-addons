#pragma once
// EventPicker — the event picker behind the edit toolbar's "Event" button (D8; NON-PURE:
// ImGui). Search field on top, the events below grouped in collapsible categories (wiki
// order); while searching only categories with hits show, expanded. Rows: name + map(s), no
// countdown. Search and data: Events.h.

#include <string>

namespace Magpie { namespace EventPicker {

// Draw the picker's contents inside an open popup. `query` is the search text (kept by the
// caller between frames); `focusSearch` focuses the search field once and is then cleared.
// Returns the id of the picked event (click, or Enter for the first hit), else "".
std::string Render(std::string& query, bool& focusSearch);

}} // namespace Magpie::EventPicker
