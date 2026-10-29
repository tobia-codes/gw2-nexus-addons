// FileDialog.cpp — see FileDialog.h.
#include "FileDialog.h"

#include <windows.h>
#include <commdlg.h>
#include "I18n.h"

namespace FileDialog {

std::wstring Widen(const std::string& utf8)
{
    if (utf8.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &w[0], n);
    return w;
}

std::wstring JsonFilter(const char* label)
{
    std::wstring f;
    f += Widen(label);                                    f += L'\0'; f += L"*.json"; f += L'\0';
    f += Widen(Trailpack::I18n::Tr("All files (*.*)"));      f += L'\0'; f += L"*.*";    f += L'\0';
    return f;
}

std::wstring Run(bool save, std::wstring dir, std::wstring fileName,
                 std::wstring title, std::wstring filter)
{
    wchar_t buf[MAX_PATH * 4] = L"";
    if (save) fileName.copy(buf, (sizeof buf / sizeof buf[0]) - 1);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize     = sizeof ofn;
    ofn.lpstrFilter     = filter.c_str();   // "label\0pattern\0...\0" (see JsonFilter)
    ofn.lpstrFile       = buf;
    ofn.nMaxFile        = sizeof buf / sizeof buf[0];
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt     = L"json";
    ofn.lpstrTitle      = title.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? std::wstring(buf) : std::wstring();
}

} // namespace FileDialog
