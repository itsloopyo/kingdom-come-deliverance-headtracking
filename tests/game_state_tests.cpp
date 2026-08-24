#include "test_support.h"

#include "game_state.h"

using kcd_ht::InputSignals;
using kcd_ht::IsGameplay;
using kcd_tests::Check;

int RunGameStateTests()
{
    int failures = 0;
    std::cout << "Game state\n";

    {
        InputSignals playing;
        playing.foreground = true;
        playing.cursorShowing = false;
        playing.cursorClipped = true;
        Check(failures, IsGameplay(playing), "a hidden, clipped cursor in the foreground is play");
    }

    {
        InputSignals menu;
        menu.foreground = true;
        menu.cursorShowing = true;
        menu.cursorClipped = false;
        Check(failures, !IsGameplay(menu), "a visible, released cursor is a menu");
    }

    {
        // Cursor visibility is desktop-global, so an overlay drawing a cursor
        // over the game must not read as a menu while the game holds the clip.
        InputSignals overlay;
        overlay.foreground = true;
        overlay.cursorShowing = true;
        overlay.cursorClipped = true;
        Check(failures, IsGameplay(overlay), "a shown cursor the game still clips is play");
    }

    {
        // Alt-tabbed: the mouse belongs to whatever is in front, so neither half
        // of the mouse test can be trusted.
        InputSignals alttabbed;
        alttabbed.foreground = false;
        alttabbed.cursorShowing = false;
        alttabbed.cursorClipped = true;
        Check(failures, !IsGameplay(alttabbed), "another window in front is never play");
    }

    return kcd_tests::Report("Game state", failures);
}
