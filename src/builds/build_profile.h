#pragma once

#include <cstdint>

#include <cameraunlock/memory/pe_fingerprint.h>

namespace kcd_ht::builds
{
    // Every address this mod pins to a specific game build, in one place. Call
    // sites read builds::ActiveProfile().Offsets.<name> and never see a literal.
    //
    // The addresses are RVAs into WHGame.dll, not into KingdomCome.exe: the exe
    // is a 1.3 MB launcher stub and the entire engine, including the view system,
    // lives in WHGame.dll. So the fingerprint below is WHGame.dll's too.

    // Kingdom Come positions its crosshair from four call sites.
    constexpr int kMaxCursorReturnSites = 4;

    struct OffsetTable
    {
        // CView::Update(CView* this, float frameTime, bool isActive). Slot 2 of
        // CView::vftable. Composes m_viewParams into the camera matrix and ends
        // by calling CCamera::UpdateFrustum on it.
        std::uint32_t kCViewUpdateRva;

        // CCamera::UpdateFrustum(CCamera* this). Rebuilds the frustum planes from
        // the camera matrix; must be re-run after the matrix is modified or
        // culling is computed against the pre-injection view.
        std::uint32_t kCCameraUpdateFrustumRva;

        // Byte offset of CView::m_camera. Its first member is the Matrix34 that
        // CView::Update writes, so this offset IS the matrix address.
        std::uint32_t kCViewCameraOffset;

        // Byte offset of CView::m_viewParams. Never written by this mod - it is
        // what the game reads for aim, raycasts and weapon direction, and leaving
        // it alone is what decouples look from aim. Recorded so the log can prove
        // the layout matched at runtime.
        std::uint32_t kCViewParamsOffset;

        // Size of a CCamera, read off CCamera::operator= (it copies fields up to
        // +0x25C). The injection hands the renderer a COPY of the camera this big
        // rather than editing the game's own, so no other reader - on this thread
        // or any worker - can ever observe the head pose.
        std::uint32_t kCameraSize;

        // Builds the frame's rendering pass info from a camera -
        // (SRenderingPassInfo* out, CCamera* camera, int flags). The pass info
        // takes its own copy, so a head pose applied across this one call reaches
        // the picture and nothing else.
        //
        // The window has to be exactly this narrow, and that was established the
        // hard way: applying the pose in CView::Update leaves it in the camera
        // the game reads back through GetViewCamera from 141 call sites, and
        // interaction dies once the head turns 15 degrees off what the mouse is
        // pointing at. Widening back out to the whole of CSystem::Render is no
        // better - the HUD update inside that call re-reads the camera and does
        // the same thing.
        std::uint32_t kPassInfoFromCameraRva;

        // The engine's occlusion cull-buffer prep, (cullBuffer, CCamera*). It
        // builds the screen-space matrix every object's bounding box is tested
        // against, and it is handed the game's own camera rather than the one
        // the frame is drawn from - so without this the occlusion test culls
        // against the un-tracked view and objects the player is looking at
        // disappear. Both of its call sites prepare occlusion culling and
        // nothing else, which is what makes substituting the camera here safe.
        std::uint32_t kPrepareCullBufferRva;

        // How the injection knows WHICH camera is the player's: CSystem::Render
        // (ISystem vtable slot 0x048) hands this + 0x288 to the renderer, and the
        // pass-info builder runs for shadow and reflection cameras too, which
        // must be left alone.
        std::uint32_t kSystemRenderRva;
        std::uint32_t kSystemViewCameraOffset;

        // Byte offsets into CCamera of the two frustum fields the reticle needs:
        // the VERTICAL field of view in radians, and the width-to-height ratio.
        // Both are written by CCamera::SetFrustum, so reading them after
        // CView::Update gives the live values the frame is rendered with.
        std::uint32_t kCCameraFovOffset;
        std::uint32_t kCCameraProjectionRatioOffset;

        // The one helper every HUD cursor goes through:
        //   (this, element, const float posPixels[2], int mode)
        // It subtracts the screen centre from posPixels and scales the remainder
        // into Flash units, so adding the aim offset to posPixels moves the
        // cursor exactly as far as the reticle projection asks.
        std::uint32_t kSetCursorPositionRva;

        // RETURN addresses (call site + 5) of every call that positions a
        // crosshair. All four callers of the helper position one, so this is not
        // filtering anything out today; it is what keeps the offset off a caller
        // a patch might add for something else. Trailing zeroes are ignored.
        std::uint32_t kCursorReturnRvas[kMaxCursorReturnSites];

        // The renderer singleton pointer, and the vtable BYTE offsets of the two
        // getters returning the back buffer width and height. Needed because the
        // cursor position is in pixels and the aim projection is an angle.
        std::uint32_t kRendererGlobalRva;
        std::uint32_t kRendererWidthSlot;
        std::uint32_t kRendererHeightSlot;

        // The engine console, and the three vtable BYTE offsets the field of
        // view option needs: IConsole::GetCVar(name), then ICVar::GetFVal() and
        // ICVar::Set(float) on what it returns. Writing `cl_fov` through the
        // console is how the game's own FOV slider does it, so the whole engine
        // - frustum, culling, HUD projection - follows a change made this way.
        std::uint32_t kConsoleGlobalRva;
        std::uint32_t kConsoleGetCVarSlot;
        std::uint32_t kCVarGetFValSlot;
        std::uint32_t kCVarSetFloatSlot;
    };

    struct BuildProfile
    {
        const char* Name;
        cameraunlock::memory::PeFingerprint Fingerprint;
        OffsetTable Offsets;
    };

    // A profile whose hook target is still zero is a placeholder landed ahead of
    // the rederive; it matches by fingerprint but must never activate.
    inline bool IsComplete(const BuildProfile& profile)
    {
        return profile.Offsets.kCViewUpdateRva != 0
            && profile.Offsets.kCCameraUpdateFrustumRva != 0;
    }
}
