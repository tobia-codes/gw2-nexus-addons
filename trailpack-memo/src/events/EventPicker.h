#pragma once
// EventPicker — the event picker behind the edit toolbar's "Event" button (D8; NON-PURE:
// ImGui). Search field on top, the events below grouped in collapsible categories (wiki
// order); while searching only categories with hits show, expanded. Rows: name + map(s), no
// countdown. Also the compact inline list for "/e " in the editor (D9). Search and data come
// from Trailpack Core (CoreClient).

#include <string>
#include <vector>
#include "integrations/CoreClient.h"

namespace Trailpack { namespace EventPicker {

// Draw the picker's contents inside an open popup. `query` is the search text (kept by the
// caller between frames); `focusSearch` focuses the search field once and is then cleared.
// Returns the id of the picked event (click, or Enter for the first hit), else "".
std::string Render(std::string& query, bool& focusSearch);

// Most rows of the inline list.
constexpr int kMaxSuggestions = 8;

// Draw the inline list's rows inside an open popup: name + grey category abbreviation, row
// `selected` highlighted. Returns the id of a clicked row, else "".
std::string RenderSuggestions(const std::vector<const CoreClient::Event*>& hits, int selected);

}} // namespace Trailpack::EventPicker
