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
    // The remote command "lives" calls this
    void    SetLives(bool (*action)(void));
    // The remote command "save" calls this
    void    SetSave(bool (*action)(void));
    // (world << 8) | number of a level file every visit should load, 0 for none
    u32     ForcedLevel(void);
    // true if areas 1 and 2 of every level should trade places, to start in the second one
    bool    SwapAreas(void);
    // "startat=N" puts the start of a level's first area onto its entrance N, a door for one
    void    MoveStart(u8 *course);
    // "lseed=N" replays one exact roll of a level, the one a console log came from
    bool    LevelSeed(u32 &seed);
    // "dropsecret=1": treat the level as one whose secret exit has to go
    bool    DropSecret(void);
    // "areas=mask": enemies are only swapped in the areas whose bit is set, 0 means all
    bool    AreaEnemies(u32 index);
    // "area=N;entr=E": a visit starts in area N (1..4) at its entrance E instead of the level start
    bool    StartArea(u32 &area, u32 &entrance);
#else
    inline void MoveStart(u8 *) {}
    inline bool LevelSeed(u32 &) { return false; }
    inline bool DropSecret(void) { return false; }
    inline bool AreaEnemies(u32) { return true; }
    inline bool StartArea(u32 &, u32 &) { return false; }
    inline void Init(Rando::Settings &) {}
    inline bool Active(void) { return false; }
    inline void OnFrame(u32) {}
    inline void Note(const char *) {}
    inline void SetGiveUp(bool (*)(void)) {}
    inline void SetLives(bool (*)(void)) {}
    inline void SetSave(bool (*)(void)) {}
    inline u32  ForcedLevel(void) { return 0; }
    inline bool SwapAreas(void) { return false; }
#endif
}

#endif
