// Consistency checks on the shipped build profiles. The RVAs themselves can only
// be confirmed against the game, but the relationships between them are
// checkable here, and each one has a failure mode that reaches a player: a zero
// hook RVA leaves the mod permanently dormant, a camera offset that collides
// with m_viewParams would write over the values the game aims with, and two
// profiles sharing a fingerprint would route half the players to the wrong RVAs.

#include "builds/build_registry.h"

#include "test_support.h"

namespace
{
    using kcd_tests::Check;
    using kcd_ht::builds::BuildProfile;

    // A CryEngine Matrix34 is twelve floats. m_viewParams starting inside that
    // span would mean the injection silently rewrote the values the game aims
    // with.
    constexpr std::uint32_t kMatrix34Bytes = 48;

    // Every store's profile for the build the mod was written against. The
    // registry array itself lives in build_registry.cpp, which only compiles
    // against a live game; these are the same objects it points at.
    const BuildProfile* const kProfiles[] = {
        &kcd_ht::builds::kGdkProfile_20260401,
        &kcd_ht::builds::kSteamProfile_20260401,
    };

    void CheckProfile(int& failures, const BuildProfile& profile)
    {
        const kcd_ht::builds::OffsetTable& offsets = profile.Offsets;
        const std::string name(profile.Name);

        Check(failures, IsComplete(profile),
              name + ": the profile is complete, so the mod activates rather than "
                     "staying dormant");

        Check(failures, offsets.kCViewUpdateRva < profile.Fingerprint.SizeOfImage
                     && offsets.kCCameraUpdateFrustumRva < profile.Fingerprint.SizeOfImage
                     && offsets.kPassInfoFromCameraRva < profile.Fingerprint.SizeOfImage
                     && offsets.kPrepareCullBufferRva < profile.Fingerprint.SizeOfImage
                     && offsets.kSystemRenderRva < profile.Fingerprint.SizeOfImage
                     && offsets.kSetCursorPositionRva < profile.Fingerprint.SizeOfImage
                     && offsets.kRendererGlobalRva < profile.Fingerprint.SizeOfImage
                     && offsets.kConsoleGlobalRva < profile.Fingerprint.SizeOfImage,
              name + ": every pinned RVA lands inside the module image");

        // The world pass is a call site INSIDE CSystem::Render, so its return
        // address has to land within that function. A zero, or an address
        // outside it, means the reticle would be scaled by whichever pass
        // happened to build last - the fault this field exists to prevent.
        Check(failures, offsets.kGeneralPassReturnRva > offsets.kSystemRenderRva
                     && offsets.kGeneralPassReturnRva - offsets.kSystemRenderRva < 0x1000,
              name + ": the world pass return address is inside CSystem::Render");

        // m_viewParams is 12 floats of position + quaternion + more starting at
        // 0x14; the camera has to sit past it, and the mod must never write into it.
        Check(failures, offsets.kCViewCameraOffset > offsets.kCViewParamsOffset,
              name + ": the camera sits after m_viewParams in CView");

        Check(failures, offsets.kCViewParamsOffset + kMatrix34Bytes <= offsets.kCViewCameraOffset,
              name + ": the camera matrix cannot overlap m_viewParams");

        // A return address is the instruction after a five-byte call, so it can
        // never equal the callee's own entry point, and a duplicate would apply
        // the aim offset twice at one site.
        for (int i = 0; i < kcd_ht::builds::kMaxCursorReturnSites; ++i)
        {
            const std::uint32_t site = offsets.kCursorReturnRvas[i];
            if (site == 0) continue;
            Check(failures, site != offsets.kSetCursorPositionRva
                         && site < profile.Fingerprint.SizeOfImage,
                  name + ": cursor return site " + std::to_string(i) + " is a distinct "
                         "in-image address");
            for (int j = 0; j < i; ++j)
            {
                Check(failures, offsets.kCursorReturnRvas[j] != site,
                      name + ": cursor return site " + std::to_string(i) + " is not a "
                             "duplicate of " + std::to_string(j));
            }
        }
    }
}

int RunBuildProfileTests()
{
    int failures = 0;
    std::cout << "Build profile tests\n";

    // Read from the shipped WHGame.dll - not the exe, which is a launcher stub
    // with no camera code in it. Steam keeps both under Bin\Win64; the Game Pass
    // package puts them flat in Content.
    Check(failures,
          kcd_ht::builds::kSteamProfile_20260401.Fingerprint.TimeDateStamp == 0x69CCD815u
       && kcd_ht::builds::kSteamProfile_20260401.Fingerprint.SizeOfImage == 0x039EB000u
       && kcd_ht::builds::kSteamProfile_20260401.Fingerprint.CheckSum == 0x00000000u,
          "the Steam 2026-04-01 WHGame.dll fingerprint is unchanged");

    Check(failures,
          kcd_ht::builds::kGdkProfile_20260401.Fingerprint.TimeDateStamp == 0x69CCF8C1u
       && kcd_ht::builds::kGdkProfile_20260401.Fingerprint.SizeOfImage == 0x03A77000u
       && kcd_ht::builds::kGdkProfile_20260401.Fingerprint.CheckSum == 0x00000000u,
          "the Game Pass 2026-04-01 WHGame.dll fingerprint is unchanged");

    constexpr int kProfileCount = static_cast<int>(sizeof(kProfiles) / sizeof(kProfiles[0]));
    for (int i = 0; i < kProfileCount; ++i)
    {
        CheckProfile(failures, *kProfiles[i]);
        for (int j = 0; j < i; ++j)
        {
            Check(failures, !kProfiles[i]->Fingerprint.Matches(kProfiles[j]->Fingerprint),
                  std::string(kProfiles[i]->Name) + " and " + kProfiles[j]->Name
                      + " are told apart by fingerprint");
        }
    }

    // The two stores ship separate links of the same source revision, so the
    // struct layout has to agree even though every RVA differs. A future profile
    // that disagrees here is a layout change, not a relink, and the injection
    // maths would need revisiting rather than just the addresses.
    const kcd_ht::builds::OffsetTable& steam = kcd_ht::builds::kSteamProfile_20260401.Offsets;
    const kcd_ht::builds::OffsetTable& gdk = kcd_ht::builds::kGdkProfile_20260401.Offsets;
    Check(failures, steam.kCViewCameraOffset == gdk.kCViewCameraOffset
                 && steam.kCViewParamsOffset == gdk.kCViewParamsOffset
                 && steam.kCameraSize == gdk.kCameraSize
                 && steam.kSystemViewCameraOffset == gdk.kSystemViewCameraOffset
                 && steam.kCCameraFovOffset == gdk.kCCameraFovOffset
                 && steam.kCCameraProjectionRatioOffset == gdk.kCCameraProjectionRatioOffset,
          "the Steam and Game Pass builds agree on every struct offset");

    Check(failures, steam.kRendererWidthSlot == gdk.kRendererWidthSlot
                 && steam.kRendererHeightSlot == gdk.kRendererHeightSlot
                 && steam.kConsoleGetCVarSlot == gdk.kConsoleGetCVarSlot
                 && steam.kCVarGetFValSlot == gdk.kCVarGetFValSlot
                 && steam.kCVarSetFloatSlot == gdk.kCVarSetFloatSlot,
          "the Steam and Game Pass builds agree on every vtable slot");

    Check(failures, steam.kCViewUpdateRva != gdk.kCViewUpdateRva
                 && steam.kSetCursorPositionRva != gdk.kSetCursorPositionRva
                 && steam.kRendererGlobalRva != gdk.kRendererGlobalRva,
          "the two builds are separate links, so their RVAs differ");

    return kcd_tests::Report("Build profile tests", failures);
}
