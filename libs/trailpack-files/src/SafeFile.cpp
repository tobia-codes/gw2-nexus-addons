// SafeFile.cpp — see SafeFile.h.
#include "tpfiles/SafeFile.h"

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <sys/stat.h>
#endif

namespace Trailpack { namespace SafeFile {

namespace {

#ifdef _WIN32
const char kSep = '\\';

std::wstring Wide(const std::string& utf8)
{
    if (utf8.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &w[0], n);
    return w;
}

FILE* Open(const std::string& path, const wchar_t* mode) { return _wfopen(Wide(path).c_str(), mode); }
#else
const char kSep = '/';

FILE* Open(const std::string& path, const wchar_t* mode)
{
    return std::fopen(path.c_str(), mode[0] == L'r' ? "rb" : "wb");
}
#endif

bool IsSep(char c) { return c == '/' || c == '\\'; }

// Replace `to` with `from` in one step (target may exist).
bool ReplaceFile(const std::string& from, const std::string& to)
{
#ifdef _WIN32
    return MoveFileExW(Wide(from).c_str(), Wide(to).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(from.c_str(), to.c_str()) == 0;   // POSIX rename replaces atomically
#endif
}

bool CopyOver(const std::string& from, const std::string& to)
{
#ifdef _WIN32
    return CopyFileW(Wide(from).c_str(), Wide(to).c_str(), FALSE) != 0;
#else
    std::string content;
    return Read(from, content) && Write(to, content);
#endif
}

} // namespace

bool Read(const std::string& path, std::string& out)
{
    FILE* f = Open(path, L"rb");
    if (f == nullptr) return false;
    std::string data;
    char buf[16384];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    if (ok) out = std::move(data);
    return ok;
}

bool Write(const std::string& path, const std::string& content)
{
    FILE* f = Open(path, L"wb");
    if (f == nullptr) return false;
    bool ok = std::fwrite(content.data(), 1, content.size(), f) == content.size();
    ok = std::fflush(f) == 0 && ok;
    return std::fclose(f) == 0 && ok;
}

std::string BackupPath(const std::string& path)
{
    return path + ".bak";
}

bool WriteAtomic(const std::string& path, const std::string& content, bool keepBackup)
{
    const std::string tmp = path + ".tmp";
    if (!Write(tmp, content)) {
        Remove(tmp);
        return false;
    }

    if (keepBackup && Exists(path))
        CopyOver(path, BackupPath(path));

    if (!ReplaceFile(tmp, path)) {
        Remove(tmp);
        return false;
    }
    return true;
}

std::string SetAside(const std::string& path)
{
    const std::string name = FileName(path);
    const size_t dot = name.find_last_of('.');
    const bool hasExt = dot != std::string::npos && dot != 0;
    const std::string stem = hasExt ? name.substr(0, dot) : name;
    const std::string ext  = hasExt ? name.substr(dot) : std::string();
    const std::string dir  = ParentPath(path);
    for (int n = 1; n < 1000; ++n) {
        const std::string file = stem + ".corrupt-" + std::to_string(n) + ext;
        const std::string target = dir.empty() ? file : Join(dir, file);
        if (Exists(target)) continue;
#ifdef _WIN32
        const bool ok = MoveFileW(Wide(path).c_str(), Wide(target).c_str()) != 0;
#else
        const bool ok = std::rename(path.c_str(), target.c_str()) == 0;
#endif
        return ok ? target : std::string();
    }
    return std::string();
}

// ── Paths ─────────────────────────────────────────────────────────────────

bool Exists(const std::string& path)
{
#ifdef _WIN32
    return GetFileAttributesW(Wide(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

bool Remove(const std::string& path)
{
#ifdef _WIN32
    return DeleteFileW(Wide(path).c_str()) != 0;
#else
    return std::remove(path.c_str()) == 0;
#endif
}

bool CreateDirectories(const std::string& dir)
{
    if (dir.empty()) return false;
    // Create each prefix that ends before a separator, then the full path; existing
    // components (and a drive like "C:") simply fail and are skipped.
    for (size_t i = 1; i <= dir.size(); ++i) {
        if (i < dir.size() && !IsSep(dir[i])) continue;
        const std::string part = dir.substr(0, i);
#ifdef _WIN32
        CreateDirectoryW(Wide(part).c_str(), nullptr);
#else
        mkdir(part.c_str(), 0777);
#endif
    }
    return Exists(dir);
}

std::string Join(const std::string& dir, const std::string& name)
{
    if (dir.empty()) return name;
    return IsSep(dir.back()) ? dir + name : dir + kSep + name;
}

std::string FileName(const std::string& path)
{
    size_t i = path.size();
    while (i > 0 && !IsSep(path[i - 1])) --i;
    return path.substr(i);
}

std::string ParentPath(const std::string& path)
{
    size_t i = path.size();
    while (i > 0 && !IsSep(path[i - 1])) --i;
    while (i > 0 && IsSep(path[i - 1])) --i;   // "a//b" -> "a"
    return path.substr(0, i);
}

}} // namespace Trailpack::SafeFile
