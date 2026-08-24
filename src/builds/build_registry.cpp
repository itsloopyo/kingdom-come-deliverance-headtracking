#include "build_registry.h"

#include <iterator>

#include "../logging.h"

namespace kcd_ht::builds
{
    namespace
    {
        using cameraunlock::memory::FingerprintMismatch;
        using cameraunlock::memory::PeFingerprint;

        const BuildProfile* g_active = nullptr;

        const char* MismatchAdvice(FingerprintMismatch how)
        {
            switch (how)
            {
            case FingerprintMismatch::Newer:
                return "the game is NEWER than any build this mod knows about - check the "
                       "releases page for an updated mod";
            case FingerprintMismatch::Older:
                return "the game is OLDER than the newest profile - let Steam finish updating";
            case FingerprintMismatch::Differs:
                return "same build date but a different size or checksum - a repacked or "
                       "modified WHGame.dll, which this mod will not engage on";
            }
            return "unclassified fingerprint mismatch";
        }
    }

    const BuildProfile* const kKnownProfiles[] = {
        &kSteamProfile_20260401,
    };
    const int kKnownProfileCount = static_cast<int>(std::size(kKnownProfiles));

    SelectResult SelectProfile(void* whGameModule)
    {
        PeFingerprint running{};
        if (!cameraunlock::memory::ReadPeFingerprint(whGameModule, running))
        {
            Log::Line("WHGame.dll PE header could not be read - staying dormant.");
            return SelectResult::NoMatch;
        }

        Log::Line("WHGame.dll fingerprint: ts=0x%08X size=0x%08X csum=0x%08X",
                  running.TimeDateStamp, running.SizeOfImage, running.CheckSum);

        for (int i = 0; i < kKnownProfileCount; ++i)
        {
            const BuildProfile* candidate = kKnownProfiles[i];
            const bool matches = running.Matches(candidate->Fingerprint);
            Log::Line("  profile %-24s ts=0x%08X size=0x%08X csum=0x%08X  %s",
                      candidate->Name,
                      candidate->Fingerprint.TimeDateStamp,
                      candidate->Fingerprint.SizeOfImage,
                      candidate->Fingerprint.CheckSum,
                      matches ? "MATCH" : "no");
            if (!matches) continue;

            if (!IsComplete(*candidate))
            {
                Log::Line("Profile %s is a placeholder with no hook address yet - staying "
                          "dormant. The game runs unmodified.", candidate->Name);
                return SelectResult::Incomplete;
            }
            g_active = candidate;
            return SelectResult::Matched;
        }

        const BuildProfile& primary = *kKnownProfiles[0];
        Log::Line("No profile matches this WHGame.dll - staying dormant, the game runs "
                  "unmodified. Diagnosis: %s.",
                  MismatchAdvice(cameraunlock::memory::ClassifyMismatch(running, primary.Fingerprint)));
        Log::Line("Run 'pixi run check-fingerprint' against your install and open an issue "
                  "with the line above.");
        return SelectResult::NoMatch;
    }

    const BuildProfile& ActiveProfile() { return *g_active; }
    const OffsetTable& Offsets() { return g_active->Offsets; }
}
