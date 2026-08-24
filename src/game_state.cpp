#include "game_state.h"

#include <windows.h>

#include <atomic>
#include <cstdint>

#include "logging.h"

namespace kcd_ht
{
    namespace
    {
        constexpr std::uint64_t kSampleIntervalMs = 100;

        // Windows reports an unclipped cursor as the whole virtual screen, so
        // anything narrower means an application has taken the mouse.
        bool CursorIsClipped()
        {
            RECT clip{};
            if (!GetClipCursor(&clip)) return false;
            const long vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
            const long vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
            const long vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            const long vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            return !(clip.left <= vx && clip.top <= vy
                     && clip.right >= vx + vw && clip.bottom >= vy + vh);
        }

        // Atomic because the gate is asked from the render path as well as the
        // view update, and CryEngine does not promise those are the same thread.
        // The worst a race can do here is sample the cursor twice in one interval.
        struct GateCache
        {
            std::atomic<std::uint64_t> lastSampleMs{0};
            std::atomic<bool> gameplay{false};
            std::atomic<bool> sampled{false};
        };

        GateCache g_gate;

        // Which of the three signals moved is the whole diagnostic when a game
        // state turns out not to be covered by IsGameplay.
        void LogGateTransition(bool gameplay, const InputSignals& signals)
        {
            Log::Line("gameplay gate %s (foreground=%s cursor=%s clip=%s)",
                      gameplay ? "OPEN - tracking applied" : "CLOSED - tracking suppressed",
                      signals.foreground ? "yes" : "no",
                      signals.cursorShowing ? "shown" : "hidden",
                      signals.cursorClipped ? "yes" : "no");
        }
    }

    bool IsGameplay(const InputSignals& signals)
    {
        // Alt-tabbed is never gameplay, whatever the mouse is doing: another
        // application owning the cursor says nothing about this one.
        if (!signals.foreground) return false;
        // Kingdom Come hides the cursor and clips it to the window to play, and
        // hands both back for the main menu, the pause menu, the inventory, the
        // map and the codex. Either half is enough - cursor visibility is global
        // desktop state, so an overlay showing a cursor must not read as a menu.
        return !signals.cursorShowing || signals.cursorClipped;
    }

    InputSignals SampleInputSignals()
    {
        InputSignals signals;

        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        signals.foreground = pid == GetCurrentProcessId();

        CURSORINFO info{};
        info.cbSize = sizeof(info);
        // A failed query reads as "cursor hidden", which is the gameplay side of
        // the test: the clip check below still has to agree for the gate to open.
        signals.cursorShowing = GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING) != 0;

        signals.cursorClipped = CursorIsClipped();
        return signals;
    }

    bool InActiveGameplay()
    {
        const std::uint64_t now = GetTickCount64();
        const bool sampled = g_gate.sampled.load(std::memory_order_relaxed);
        if (sampled && now - g_gate.lastSampleMs.load(std::memory_order_relaxed) < kSampleIntervalMs)
            return g_gate.gameplay.load(std::memory_order_relaxed);
        g_gate.lastSampleMs.store(now, std::memory_order_relaxed);

        const InputSignals signals = SampleInputSignals();
        const bool gameplay = IsGameplay(signals);
        if (!sampled || gameplay != g_gate.gameplay.load(std::memory_order_relaxed))
            LogGateTransition(gameplay, signals);

        g_gate.sampled.store(true, std::memory_order_relaxed);
        g_gate.gameplay.store(gameplay, std::memory_order_relaxed);
        return gameplay;
    }
}
