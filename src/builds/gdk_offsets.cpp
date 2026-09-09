#include "build_registry.h"

// Every Microsoft Store / Xbox Game Pass build of Kingdom Come: Deliverance this
// mod knows about, oldest at the bottom. APPEND ONLY, exactly as in
// steam_offsets.cpp: a new game build gets a NEW profile, never an edit to an
// existing one.
//
// The GDK build is a separate link of the same source revision as the Steam
// build - both WHGame.dll headers carry a 2026-04-01 TimeDateStamp, and every
// struct offset below came out identical - but the code was laid out
// differently, so every RVA moved. That is why this is a profile and not a
// shared table: the two binaries agree on what the engine IS and disagree on
// where all of it lives.
//
// The Game Pass layout is flat. KingdomCome.exe and WHGame.dll sit directly in
// <XboxGames>\Kingdom Come- Deliverance\Content, not under Bin\Win64, so the
// loader, the ASI, HeadTracking.ini and HeadTracking.log all sit there instead.
// Nothing in the mod depends on that - the log and INI follow the running exe -
// but it is the first thing to check when a Game Pass player reports no log file.

namespace kcd_ht::builds
{
    // Microsoft Store package DeepSilver.KingdomComeDeliverance 1.9.7.0
    // (MicrosoftGame.config), whdlversions.txt "Build = 404-503". WHGame.dll
    // built 2026-04-01 10:51:45 UTC.
    extern const BuildProfile kGdkProfile_20260401 = {
        /* Name        */ "gdk-win64-20260401",
        /* Fingerprint */ { 0x69CCF8C1u, 0x03A77000u, 0x00000000u },
        /* Offsets     */ {
            /* kCViewUpdateRva               */ 0x0032245Cu,
            /* kCCameraUpdateFrustumRva      */ 0x00320D10u,
            /* kCViewCameraOffset            */ 0x000000E8u,
            /* kCViewParamsOffset            */ 0x00000014u,
            /* kCameraSize                   */ 0x00000260u,
            /* kPassInfoFromCameraRva        */ 0x003733B8u,
            /* kGeneralPassReturnRva         */ 0x003FC508u,
            /* kPrepareCullBufferRva         */ 0x002F8014u,
            /* kSystemRenderRva              */ 0x003FC370u,
            /* kSystemViewCameraOffset       */ 0x00000288u,
            /* kCCameraFovOffset             */ 0x00000030u,
            /* kCCameraProjectionRatioOffset */ 0x00000040u,
            /* kSetCursorPositionRva         */ 0x00620930u,
            /* kCursorReturnRvas             */ {
                0x0020B34Au,  // CursorCross parked at screen centre, mode 1
                0x010D389Du,  // CombatCursorCross, an already-computed aim point
                0x010D3C03u,  // CursorCross, computed point or centre, mode 2
                0x010D3D5Au,  // CursorCross parked at screen centre, mode 3
            },
            /* kRendererGlobalRva            */ 0x02A55C40u,
            /* kRendererWidthSlot            */ 0x00000230u,
            /* kRendererHeightSlot           */ 0x00000228u,
            /* kConsoleGlobalRva             */ 0x02A55BE8u,
            /* kConsoleGetCVarSlot           */ 0x000000B8u,
            /* kCVarGetFValSlot              */ 0x00000020u,
            /* kCVarSetFloatSlot             */ 0x00000040u,
        },
    };
}
