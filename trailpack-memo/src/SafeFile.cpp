// SafeFile.cpp — see SafeFile.h.
#include "SafeFile.h"

#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace Trailpack { namespace SafeFile {

// Replace `to` with `from` in one step (target may exist).
static bool ReplaceFile(const fs::path& from, const fs::path& to)
{
#ifdef _WIN32
    return MoveFileExW(from.wstring().c_str(), to.wstring().c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    fs::rename(from, to, ec);   // POSIX rename replaces atomically
    return !ec;
#endif
}

bool Read(const fs::path& path, std::string& out)
{
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f.is_open()) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    if (f.bad()) return false;
    out = ss.str();
    return true;
}

fs::path BackupPath(const fs::path& path)
{
    fs::path p = path;
    p += ".bak";
    return p;
}

bool WriteAtomic(const fs::path& path, const std::string& content, bool keepBackup)
{
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!f.is_open()) return false;
        f << content;
        f.flush();
        if (!f.good()) {
            f.close();
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }

    std::error_code ec;
    if (keepBackup && fs::exists(path, ec))
        fs::copy_file(path, BackupPath(path), fs::copy_options::overwrite_existing, ec);

    if (!ReplaceFile(tmp, path)) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

fs::path SetAside(const fs::path& path)
{
    std::error_code ec;
    for (int n = 1; n < 1000; ++n) {
        fs::path target = path.parent_path() /
            (path.stem().string() + ".corrupt-" + std::to_string(n) + path.extension().string());
        if (fs::exists(target, ec)) continue;
        fs::rename(path, target, ec);
        return ec ? fs::path() : target;
    }
    return fs::path();
}

}} // namespace Trailpack::SafeFile
