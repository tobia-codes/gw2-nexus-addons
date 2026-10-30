#pragma once
// SafeFile.h — PURE crash-safe file I/O and path helpers (no ImGui/Nexus).
//
// Paths are UTF-8 std::strings; on Windows every call goes through the wide (W) Win32 API.
// Deliberately no <fstream>/<sstream>/<filesystem>: with the static libstdc++ they pull
// ~1 MB of locale/iostream code into the DLL.
//
// WriteFileAtomic never leaves a half-written target: it writes "<path>.tmp", flushes it,
// optionally keeps the previous file as "<path>.bak", then swaps the new file in with a
// single rename (MoveFileExW on Windows, rename(2) elsewhere). A crash at any point leaves
// either the old or the new complete file.

#include <string>

namespace Trailpack { namespace SafeFile {

// Whole file into `out` (binary). False if it can't be opened or read.
bool Read(const std::string& path, std::string& out);

// Replace `path` with `content` directly (no temp file). False if it can't be written.
bool Write(const std::string& path, const std::string& content);

// Atomically replace `path` with `content`. With `keepBackup`, the file being replaced is
// first copied to BackupPath(path). False (target untouched) if anything fails.
bool WriteAtomic(const std::string& path, const std::string& content,
                 bool keepBackup = false);

// "<path>.bak"
std::string BackupPath(const std::string& path);

// Move an unreadable file out of the way: "<stem>.corrupt-<n><ext>" next to it (first free
// n), so it can be inspected and is never overwritten. Returns the new path, or empty.
std::string SetAside(const std::string& path);

// ── Paths ─────────────────────────────────────────────────────────────────

bool Exists(const std::string& path);
bool Remove(const std::string& path);
// Create `dir` and any missing parents. True if it exists afterwards.
bool CreateDirectories(const std::string& dir);

// "dir<sep>name" (sep: '\\' on Windows, '/' elsewhere; none added after a trailing one).
std::string Join(const std::string& dir, const std::string& name);
// Last component ("C:\\a\\b.json" -> "b.json"); '/' and '\\' both separate.
std::string FileName(const std::string& path);
// Everything before the last separator ("" if there is none).
std::string ParentPath(const std::string& path);

}} // namespace Trailpack::SafeFile
