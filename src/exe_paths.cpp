#include "exe_paths.h"

#include <windows.h>

#include <system_error>

namespace kcd_ht
{
    std::wstring DirectoryOf(const std::wstring& path)
    {
        const auto slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? L"." : path.substr(0, slash);
    }

    std::string DirectoryOf(const std::string& path)
    {
        const auto slash = path.find_last_of("\\/");
        return slash == std::string::npos ? "." : path.substr(0, slash);
    }

    // The loop covers another thread moving the working directory between a
    // read that came back too short and the next one. A drive root comes back
    // as "C:\", whose separator is dropped like any other trailing one.
    std::wstring WorkingDirectory()
    {
        std::wstring dir(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(dir.size()), &dir[0]);
            if (length == 0)
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "GetCurrentDirectoryW");
            const bool fits = length < dir.size();
            dir.resize(length);
            if (!fits) continue;
            if (dir.back() == L'\\' || dir.back() == L'/') dir.pop_back();
            return dir;
        }
    }

    // A zero length is failure and a length of MAX_PATH is truncation - and on
    // truncation some Windows versions do not terminate the buffer at all, so
    // constructing a string from it reads off the end of the array. Neither
    // result names a real directory, so both fall back to the working directory,
    // which is where the published builds' "." put the INI and the log.
    std::wstring ExeDirectory()
    {
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return WorkingDirectory();
        return DirectoryOf(std::wstring(path, length));
    }
}
