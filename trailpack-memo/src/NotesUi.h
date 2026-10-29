#pragma once
// NotesUi.h — small ImGui helpers shared by the NotesWindow*.cpp files.

#include <string>
#include "imgui.h"
#include "NotesStore.h"

namespace NotesUi {

// Button width that fits its (translated) label, but never narrower than `minW`.
float ButtonWidth(const char* label, float minW);

// Note types (daily / weekly reset): translated name, badge colour (user-set; also used
// for the countdown), badge letter colour, countdown text ("Daily reset in 3h 12m";
// empty for a plain note).
const char* TypeName(Magpie::NoteType t);
ImVec4      TypeColor(Magpie::NoteType t);
ImVec4      TypeTextColor(Magpie::NoteType t);
std::string ResetText(Magpie::NoteType t);

// Small coloured badge ("D" / "W") at `pos`; returns its width (0 for a plain note).
float DrawTypeBadge(ImDrawList* dl, ImVec2 pos, float height, Magpie::NoteType t);

} // namespace NotesUi
