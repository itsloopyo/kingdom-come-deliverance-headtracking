#pragma once

#include <string>

// Where the game EXE lives - the log and CameraUnlock.ini sit beside it.
// DirectoryOf has a narrow overload because the frozen legacy reader opens
// HeadTracking.ini by the path's ANSI form, as the published builds did.

namespace kcd_ht
{
    // Everything before the last path separator, with no trailing separator.
    // "." when the path has none.
    std::wstring DirectoryOf(const std::wstring& path);
    std::string  DirectoryOf(const std::string& path);

    // The process's working directory as a full path, with no trailing
    // separator. The config owner refuses a relative path, so "." will not do.
    std::wstring WorkingDirectory();

    // Always a full path: the working directory when the exe path cannot be read.
    std::wstring ExeDirectory();
}
