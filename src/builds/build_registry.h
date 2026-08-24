#pragma once

#include "build_profile.h"

namespace kcd_ht::builds
{
    // Append-only. A game patch adds a profile; it never edits one, so a player
    // who has held back on an older build keeps matching their own entry when
    // they install a newer mod release.
    extern const BuildProfile kSteamProfile_20260401;

    // Newest first. The head entry is the diagnostic primary: when nothing
    // matches, the running fingerprint is compared against it to say whether the
    // game is newer, older or repacked.
    extern const BuildProfile* const kKnownProfiles[];
    extern const int kKnownProfileCount;

    enum class SelectResult
    {
        Matched,
        NoMatch,
        Incomplete,
    };

    // Fingerprints @p whGameModule and selects its profile. Must run before a
    // single hook is installed; nothing else may touch game memory until it
    // returns Matched.
    SelectResult SelectProfile(void* whGameModule);

    // Valid only after SelectProfile returned Matched.
    const BuildProfile& ActiveProfile();
    const OffsetTable& Offsets();
}
