#include "build_registry.h"

// Every Steam build of Kingdom Come: Deliverance this mod knows about, oldest at
// the bottom. APPEND ONLY: a new game build gets a NEW profile appended, never an
// edit to an existing one. Editing an existing profile's RVAs strands every player
// who has not taken the patch yet.
//
// The fingerprint is WHGame.dll's, not KingdomCome.exe's. Warhorse ships the
// engine and the game in that one 47 MB module, and both binaries are relinked
// together, so either would route correctly - but the RVAs are WHGame.dll's, and
// fingerprinting the module the offsets belong to is the only version of this
// that cannot drift.
//
// CheckSum is 0 in the shipped headers (the linker was not asked to compute one).
// That is not a problem: the triple is still matched exactly, so a repacked
// binary that DOES carry a checksum fails the match rather than mis-routing.

namespace kcd_ht::builds
{
    // Steam buildid 22623230. WHGame.dll built 2026-04-01 08:32:21 UTC.
    extern const BuildProfile kSteamProfile_20260401 = {
        /* Name        */ "steam-win64-20260401",
        /* Fingerprint */ { 0x69CCD815u, 0x039EB000u, 0x00000000u },
        /* Offsets     */ {
            /* kCViewUpdateRva               */ 0x0030227Cu,
            /* kCCameraUpdateFrustumRva      */ 0x00300B30u,
            /* kCViewCameraOffset            */ 0x000000E8u,
            /* kCViewParamsOffset            */ 0x00000014u,
            /* kCameraSize                   */ 0x00000260u,
            /* kPassInfoFromCameraRva        */ 0x00353CD8u,
            /* kPrepareCullBufferRva         */ 0x002D7E34u,
            /* kSystemRenderRva              */ 0x003E42F0u,
            /* kSystemViewCameraOffset       */ 0x00000288u,
            /* kCCameraFovOffset             */ 0x00000030u,
            /* kCCameraProjectionRatioOffset */ 0x00000040u,
            /* kSetCursorPositionRva         */ 0x0061BB20u,
            /* kCursorReturnRvas             */ {
                0x0020AC4Au,  // CursorCross parked at screen centre, mode 1
                0x01071ED5u,  // CombatCursorCross, an already-computed aim point
                0x0107223Bu,  // CursorCross, computed point or centre, mode 2
                0x01072392u,  // CursorCross parked at screen centre, mode 3
            },
            /* kRendererGlobalRva            */ 0x029D17C0u,
            /* kRendererWidthSlot            */ 0x00000230u,
            /* kRendererHeightSlot           */ 0x00000228u,
            /* kConsoleGlobalRva             */ 0x029D1768u,
            /* kConsoleGetCVarSlot           */ 0x000000B8u,
            /* kCVarGetFValSlot              */ 0x00000020u,
            /* kCVarSetFloatSlot             */ 0x00000040u,
        },
    };
}
