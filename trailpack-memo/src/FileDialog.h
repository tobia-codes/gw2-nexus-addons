#pragma once
// FileDialog.h — Windows open/save dialogs for JSON files (export/import, backups).
// RunFileDialog blocks until the user closes it: run it on a worker thread
// (std::async) so the game keeps rendering, and poll the future each frame.

#include <string>

namespace FileDialog {

std::wstring Widen(const std::string& utf8);

// File-type filter: `label` (e.g. "Trailpack Memo backup (*.json)") + *.json, then
// "All files". Pairs of NUL-terminated strings plus a final NUL.
std::wstring JsonFilter(const char* label);

// Modal open/save dialog. Returns the chosen path, or empty if cancelled.
std::wstring Run(bool save, std::wstring dir, std::wstring fileName,
                 std::wstring title, std::wstring filter);

} // namespace FileDialog
