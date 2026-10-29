#pragma once
// NotesUi.h — small ImGui helpers shared by the NotesWindow*.cpp files.

#include <string>
#include "imgui.h"
#include "NotesStore.h"

namespace NotesUi {

// Button width that fits its (translated) label, but never narrower than `minW`.
float ButtonWidth(const char* label, float minW);

// `text` cut to fit `maxWidth` px in the current font, ending in "..." when shortened (cut at
// UTF-8 character boundaries). Empty if not even "..." fits.
std::string Ellipsize(const std::string& text, float maxWidth);

// Note types (daily / weekly reset): translated name, badge colour (user-set; also used
// for the countdown), badge letter colour, countdown text ("Daily reset in 3h 12m";
// empty for a plain note).
const char* TypeName(Trailpack::NoteType t);
ImVec4      TypeColor(Trailpack::NoteType t);
ImVec4      TypeTextColor(Trailpack::NoteType t);
std::string ResetText(Trailpack::NoteType t);

// Colour of note titles in the list (user-set, else the current text colour).
ImVec4      ListTextColor();
// Note pane text (user-set, else the current text colour).
ImVec4      NoteTextColor();
// Lines of the menu button (user-set, else the current text colour).
ImVec4      MenuIconColor();
// Secondary text derived from a text colour (counts, done tasks, hints): same hue, less
// opaque, so it keeps its contrast on any background.
ImVec4      Dimmed(ImVec4 c);

// Small coloured badge ("D" / "W") at `pos`; returns its width (0 for a plain note).
float DrawTypeBadge(ImDrawList* dl, ImVec2 pos, float height, Trailpack::NoteType t);

} // namespace NotesUi
