#pragma once
// Backup.h — PURE backup file format: all notes + all settings in one file; no ImGui/Nexus.
//
//   {"format":"trailpack-memo-backup","version":1,"created":<unix seconds>,
//    "notes":<notes.json object>,"settings":<settings.json object>}

#include <cstdint>
#include <string>

namespace Trailpack { namespace Backup {

// Build a backup from the current notes.json and settings.json texts. Empty if either
// isn't valid JSON.
std::string Make(const std::string& notesJson, const std::string& settingsJson, int64_t created);

// Read a backup: the embedded notes / settings as JSON texts (for NotesStore::FromJson /
// Settings::FromJson). False if the text isn't a backup of a supported version.
bool Parse(const std::string& text, std::string& notesJson, std::string& settingsJson);

}} // namespace Trailpack::Backup
