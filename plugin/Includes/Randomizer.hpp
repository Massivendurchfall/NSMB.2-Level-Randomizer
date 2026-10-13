#ifndef RANDOMIZER_HPP
#define RANDOMIZER_HPP

#include <cstdint>

// Core of the randomizer. Works on the raw bytes of a courseN.bin only and does not
// depend on the plugin framework, so it can be tested on a PC as well.
namespace Rando
{
    enum Mode
    {
        MODE_SAFE = 0,  // walkers, flyers and swimmers each stay among themselves
        MODE_WILD = 1,  // flyers also take the place of walkers
        MODE_CHAOS = 2  // everything on land trades with everything, Thwomps and Bullet Bills join in
    };

    struct Settings
    {
        uint32_t seed;
        uint8_t  enabled;       // master switch
        uint8_t  enemies;       // swap enemies
        uint8_t  levels;        // entering a level loads a different one
        uint8_t  mode;
        uint8_t  perInstance;   // 0: each kind turns into exactly one other kind, 1: every enemy on its own
        uint8_t  castles;       // castles trade places with each other, and so do towers (needs levels as well)
        uint8_t  rerollEachTry; // reroll everything each time a level is entered
        uint8_t  swapGround;
        uint8_t  swapAir;
        uint8_t  swapWater;
        uint8_t  swapPlants;
        uint8_t  notify;
        uint8_t  autosave;      // save the game after every level that was left without losing a life
        uint8_t  noRepeats;     // a finished level does not come up again while its kind has others left
        uint8_t  itemRain;      // power-ups fall from the sky in every level, off unless asked for
    };

    void    DefaultSettings(Settings &s);

    enum { MAX_MAP_ENTRIES = 24 };

    struct Result
    {
        uint32_t outSize;       // size of the new course file
        uint16_t total;         // sprites in the course
        uint16_t swapped;       // how many of them were swapped
        uint16_t checkpoints;   // checkpoint flags taken out of play
        uint16_t rings;         // Gold Rings taken out of play
        uint16_t rain;          // item spawners added for the item rain
        uint16_t typesBefore;
        uint16_t typesAfter;
        uint8_t  mapCount;      // used entries in mapFrom/mapTo
        uint16_t mapFrom[MAX_MAP_ENTRIES];
        uint16_t mapTo[MAX_MAP_ENTRIES];
        uint16_t mapHits[MAX_MAP_ENTRIES];
    };

    // Size of a course file derived from its header, 0 if the header looks wrong
    uint32_t    CourseSize(const uint8_t *course);

    uint32_t    Crc32(const uint8_t *data, uint32_t size);

    // The memory pools of the game that a new kind of enemy eats into. The game loads what all
    // areas of a level list at once, when the level starts, so these are budgets per level.
    enum
    {
        MEM_ACTOR,      // ActorHeap: actors and the model pools of the common enemies
        MEM_MODEL,      // ModelResourceHeap: model files
        MEM_SETUP,      // ModelSetupHeap
        MEM_AREA,       // AreaHeap
        MEM_VRAM_A,     // textures and geometry on the graphics chip, first bank
        MEM_VRAM_B,     // second bank
        MEM_COUNT
    };

    enum { MAX_AREAS = 4, MAX_FRESH = 16 };

    // The item spawners the item rain puts into a level carry this in the low half of the last
    // byte of their settings (byte 11 of the sprite), a place the game does not look at. It
    // tells them from the spawner of a Toad House.
    enum { RAIN_MARK = 0xA };

    // What the plan for a level settled on, kept for the log
    struct Plan
    {
        uint32_t key;                   // CRC of the first area's course file, identifies the level
        uint8_t  known;                 // the level is in the memory table
        uint8_t  rain;                  // items rain in this level
        uint8_t  mega;                  // and the Mega Mushroom is among them
        uint8_t  freshCount;
        uint16_t fresh[MAX_FRESH];      // kinds the level did not load before
        uint16_t freeBefore[MEM_COUNT]; // KB the untouched level leaves free
        uint16_t freeAfter[MEM_COUNT];  // KB left once the new kinds are loaded as well
    };

    // Plans the enemies of a whole level: which kinds it loads anyway, and which new ones its
    // memory has room for. Has to be called before the areas of the level are randomized.
    // courses[i] is the course file of area i + 1, or nullptr if there is none or it is unknown.
    // vramPooled: the plugin lets one VRAM bank overflow into the other, so the two count as one.
    void        PlanLevel(const uint8_t *const *courses, const uint32_t *sizes, const Settings &s,
                          uint32_t levelSeed, bool vramPooled);
    const Plan  &LastPlan(void);

    // Writes a copy with swapped enemies to out. Draws on the plan made for the level.
    // Returns false if there was nothing to do or the buffer is too small, out is garbage then.
    bool        RandomizeCourse(const uint8_t *course, uint32_t size, uint8_t *out, uint32_t outCap,
                                const Settings &s, uint32_t levelSeed, uint32_t areaIndex, Result &res);

    // The levels that were finished already, one bit per level in the order of LevelSlot().
    enum { LEVEL_SLOTS = 96 };

    struct Progress
    {
        uint8_t done[LEVEL_SLOTS / 8];
    };

    // Position of a level among those that get shuffled, -1 if it is none of them
    int         LevelSlot(uint16_t code);
    bool        IsDone(const Progress &p, uint16_t code);
    void        MarkDone(Progress &p, uint16_t code);
    // How many levels of the kind this level belongs to are not finished yet
    uint32_t    LeftOfItsKind(const Progress &p, uint16_t code);
    // The levels marked in here are not handed out while their kind still has others.
    // nullptr switches that off. The core wipes the marks of a kind once it has run out.
    void        SetProgress(Progress *p);
    // True if the last call of ShuffleLevel wiped marks, so the caller knows to store them again
    bool        ProgressReset(void);

    // Level shuffle. world and number are the parts of the file name "W-N.sarc", lastGiven is
    // the level handed out before ((world << 8) | number), which is not handed out again.
    // Levels only trade places with their own kind: ordinary ones, ones with a secret exit,
    // the castles of the Koopalings, the towers. Returns false for everything else.
    bool        ShuffleLevel(const Settings &s, uint32_t tries, uint32_t world, uint32_t number, uint32_t lastGiven,
                             uint32_t &outWorld, uint32_t &outNumber);

    // Tell the core whether the place on the map that is being entered can load different levels.
    // Checkpoint flags only go where it can.
    void        SetNodeShuffled(bool shuffled);

    // True if the level handed out last has a secret exit the map has no path for.
    // RandomizeCourse then turns that goal into an ordinary one.
    bool        DropSecretExit(void);

    // Testing only: the first swappable enemy of a level turns into this ID, 0 switches it off
    void        SetForcedTarget(uint16_t id);
    // Testing: puts one more kind on the load list of the first area of a level, 0 clears them all
    void        AddExtraKind(uint16_t id);

    const char  *SpriteName(uint16_t id);
}

#endif
