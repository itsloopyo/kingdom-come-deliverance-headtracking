#pragma once

namespace kcd_ht
{
    // The three signals the gameplay gate is decided from, sampled together so
    // the decision and the log line can never disagree.
    struct InputSignals
    {
        bool foreground = false;
        bool cursorShowing = false;
        bool cursorClipped = false;
    };

    // Pure predicate over the signals, so the rule is testable without a game.
    bool IsGameplay(const InputSignals& signals);

    // Samples the signals now. Cheap, but a syscall triple - the gate rate-limits
    // it rather than calling this every frame.
    InputSignals SampleInputSignals();

    // True while the player is actually playing, as opposed to sitting in a menu
    // or having alt-tabbed away. Rate-limited and cached, so the render path may
    // ask every frame. Logs every transition with the signals behind it.
    bool InActiveGameplay();
}
