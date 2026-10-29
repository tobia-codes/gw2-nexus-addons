#pragma once
// SafeFile.h — PURE crash-safe file I/O (no ImGui/Nexus).
//
// WriteFileAtomic never leaves a half-written target: it writes "<path>.tmp", flushes it,
// optionally keeps the previous file as "<path>.bak", then swaps the new file in with a
// single rename (MoveFileExW on Windows, rename(2) elsewhere). A crash at any point leaves
// either the old or the new complete file.

#include <filesystem>
#include <string>

namespace Magpie { namespace SafeFile {

// Whole file into `out` (binary). False if it can't be opened or read.
bool Read(const std::filesystem::path& path, std::string& out);

// Atomically replace `path` with `content`. With `keepBackup`, the file being replaced is
// first copied to BackupPath(path). False (target untouched) if anything fails.
bool WriteAtomic(const std::filesystem::path& path, const std::string& content,
                 bool keepBackup = false);

// "<path>.bak"
std::filesystem::path BackupPath(const std::filesystem::path& path);

// Move an unreadable file out of the way: "<stem>.corrupt-<n><ext>" next to it (first free
// n), so it can be inspected and is never overwritten. Returns the new path, or empty.
std::filesystem::path SetAside(const std::filesystem::path& path);

}} // namespace Magpie::SafeFile
