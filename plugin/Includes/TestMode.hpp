#ifndef TESTMODE_HPP
#define TESTMODE_HPP

#include <3ds/types.h>

#include "Randomizer.hpp"

// Self test for the emulator: reads autotest.txt, presses buttons, writes heartbeats
// and screenshots. None of it is part of the regular plugin build.
namespace Test
{
#ifdef RANDO_TEST
    void    Init(Rando::Settings &settings);
    bool    Active(void);
    void    OnFrame(u32 loads);
    // May be called from the game thread, the text is written to the log on the next frame
    void    Note(const char *text);
    // The remote command "giveup" calls this
    void    SetGiveUp(bool (*action)(void));
    // (world << 8) | number of a level file every visit should load, 0 for none
    u32     ForcedLevel(void);
    // true if areas 1 and 2 of every level should trade places, to start in the second one
    bool    SwapAreas(void);
#else
    inline void Init(Rando::Settings &) {}
    inline bool Active(void) { return false; }
    inline void OnFrame(u32) {}
    inline void Note(const char *) {}
    inline void SetGiveUp(bool (*)(void)) {}
    inline u32  ForcedLevel(void) { return 0; }
    inline bool SwapAreas(void) { return false; }
#endif
}

#endif
