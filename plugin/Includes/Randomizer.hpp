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
        MODE_CHAOS = 2  // Thwomps and Bullet Bills join in on land, can get unfair
    };

    struct Settings
    {
        uint32_t seed;
        uint8_t  enabled;       // master switch
        uint8_t  enemies;       // swap enemies
        uint8_t  levels;        // entering a level loads a different one
        uint8_t  items;         // shuffle the power-ups inside blocks
        uint8_t  itemsExtra;    // also hand out Mini Mushrooms and Gold Flowers
        uint8_t  mode;
        uint8_t  perInstance;   // 0: each kind turns into exactly one other kind, 1: every enemy on its own
        uint8_t  variety;       // no longer used, the core works out how many extra kinds an area can take
        uint8_t  rerollEachTry; // reroll everything each time a level is entered
        uint8_t  swapGround;
        uint8_t  swapAir;
        uint8_t  swapWater;
        uint8_t  swapPlants;
        uint8_t  notify;
        uint8_t  writeLog;
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

    // Writes a copy with swapped enemies to out.
    // Returns false if there was nothing to do or the buffer is too small, out is garbage then.
    bool        RandomizeCourse(const uint8_t *course, uint32_t size, uint8_t *out, uint32_t outCap,
                                const Settings &s, uint32_t levelSeed, uint32_t areaIndex, Result &res);

    // Level shuffle. world and number are the parts of the file name "W-N.sarc".
    // Returns false for levels that never move (towers, castles, toad houses, cannons).
    bool        ShuffleLevel(const Settings &s, uint32_t tries, uint32_t world, uint32_t number,
                             uint32_t &outWorld, uint32_t &outNumber);

    // Shuffles the power-ups of the item blocks in a courseN_bgdatLn.bin, in place.
    // The result only depends on seed and block position, so running it twice changes nothing.
    // Blocks holding a Mini Mushroom are never changed, levels built around it need it.
    // Returns the number of blocks whose content changed.
    uint32_t    RandomizeItems(uint8_t *bgdat, uint32_t size, const Settings &s, uint32_t seed);

    // Testing only: the first swappable enemy of a level turns into this ID, 0 switches it off
    void        SetForcedTarget(uint16_t id);

    const char  *SpriteName(uint16_t id);
}

#endif
