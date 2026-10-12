#include "Randomizer.hpp"

#include <cstring>

#include "LevelTable.hpp"

namespace Rando
{
    namespace
    {
        // Layout of a courseN.bin: 17 (offset, size) pairs followed by the blocks.
        // Block 7 holds the sprites (24 bytes each), block 8 the list of sprite IDs
        // in use, which the game walks to load models and textures.
        const uint32_t  kBlockCount = 17;
        const uint32_t  kHeaderSize = kBlockCount * 8;
        const uint32_t  kBlockSprites = 7;
        const uint32_t  kBlockUsed = 8;
        const uint32_t  kSpriteSize = 24;
        const uint32_t  kMaxTypes = 128;
        const uint32_t  kMaxPool = 64;
        const uint16_t  kCheckpointFlag = 52;
        const uint16_t  kGoldRing = 206;
        // The game hangs once an area lists too many different sprites: 44 did it, 39 ran fine,
        // the busiest original area has 36. Swaps stay below this total and never add more than
        // a handful of kinds to one area.
        const uint32_t  kSafeKindTotal = 38;
        const uint32_t  kMinExtraKinds = 2;
        const uint32_t  kMaxExtraKinds = 6;
        const uint32_t  kTileObjectSize = 16;

        enum Slot
        {
            SLOT_GROUND,
            SLOT_AIR,
            SLOT_WATER,
            SLOT_PIPE_UP,
            SLOT_PIPE_DOWN,
            SLOT_PIPE_LEFT,
            SLOT_PIPE_RIGHT,
            SLOT_COUNT,
            SLOT_NONE = 0xFF
        };

        // Lowest mode in which a kind may fill a slot
        enum
        {
            S = MODE_SAFE,
            W = MODE_WILD,
            C = MODE_CHAOS,
            N = 9
        };

        struct Kind
        {
            uint16_t    id;
            uint8_t     home;   // slot it occupies in the original levels, SLOT_NONE = never replaced itself
            uint8_t     fill[3]; // tier for ground, air, water
            int8_t      feet;   // where its feet are below the sprite position, 16 per tile
            int8_t      centre; // where its body centre is below the sprite position
            uint8_t     weight; // how often it comes up compared to the others
            uint8_t     fixed;  // stays where it is put and cannot be stomped
            const char  *name;
        };

        const Kind  kKinds[] =
        {
            // Who may stand where follows what makes sense in the level: walkers on the ground,
            // flyers above it, swimmers in the water. A fish on land or a Goomba dropping out of
            // the sky is no fun, and neither is a walker at the bottom of the sea.
            // Ground enemies
            { 135, SLOT_GROUND, { S, N, N },  16,   8, 4, 0, "Goomba" },
            { 136, SLOT_GROUND, { S, N, N },  16,   8, 3, 0, "Bone Goomba" },
            { 137, SLOT_GROUND, { W, N, N },  16,  12, 1, 0, "Micro Goomba" },
            { 139, SLOT_GROUND, { S, N, N },  16,   8, 2, 0, "Goomba Tower" },
            { 165, SLOT_GROUND, { S, N, N },  16,   4, 4, 0, "Koopa Troopa" },
            { 158, SLOT_GROUND, { S, N, N },  16,   8, 3, 0, "Buzzy Beetle" },
            { 232, SLOT_GROUND, { S, N, N },  16,   8, 3, 0, "Spiny" },
            { 110, SLOT_GROUND, { S, N, N },  16,   4, 3, 0, "Dry Bones" },
            { 111, SLOT_GROUND, { S, N, N },  48,  24, 1, 0, "Big Dry Bones" },
            {  29, SLOT_GROUND, { S, N, N },  16,   8, 2, 0, "Bob-omb" },
            {  33, SLOT_GROUND, { S, N, N },  32,  16, 2, 0, "Hammer Bro" },
            {  31, SLOT_GROUND, { S, N, N },  32,  16, 2, 0, "Boomerang Bro" },
            {  32, SLOT_GROUND, { S, N, N },  32,  16, 2, 0, "Fire Bro" },
            {  34, SLOT_GROUND, { S, N, N },  48,  24, 1, 0, "Sledge Bro" },
            { 213, SLOT_GROUND, { S, N, N },  16,   0, 2, 0, "Pokey" },
            {  99, SLOT_GROUND, { S, N, N },  16,   4, 2, 0, "Wiggler" },
            { 138, SLOT_GROUND, { S, N, N },  16,   4, 3, 0, "Paragoomba" },
            {  82, SLOT_GROUND, { S, N, N },  16,   8, 2, 0, "Fire Snake" },
            { 159, SLOT_GROUND, { W, N, N },  16,   8, 2, 0, "Spike Top" },
            { 175, SLOT_GROUND, { S, N, N },  32,  24, 2, 1, "Piranha Plant" },
            { 176, SLOT_GROUND, { W, N, N },  64,  40, 1, 1, "Big Piranha Plant" },
            { 177, SLOT_GROUND, { S, N, N },  32,  16, 2, 1, "Fire Piranha Plant" },
            { 178, SLOT_GROUND, { W, N, N },  64,  32, 1, 1, "Big Fire Piranha Plant" },
            { 179, SLOT_GROUND, { S, N, N },  32,  24, 2, 1, "Bone Piranha Plant" },
            { 180, SLOT_GROUND, { W, N, N },  64,  40, 1, 1, "Big Bone Piranha Plant" },
            // Flyers
            { 185, SLOT_AIR, { S, S, N },  16,   4, 3, 0, "Koopa Paratroopa" },
            { 228, SLOT_AIR, { W, S, N },  16,   8, 3, 0, "Boo" },
            { 229, SLOT_AIR, { W, S, N },  96,  64, 1, 0, "Big Boo" },
            { 140, SLOT_AIR, { W, S, N },  16,   8, 3, 0, "Crowber" },
            {   8, SLOT_AIR, { W, S, N },  16,   8, 3, 0, "Swooper" },
            { 113, SLOT_AIR, { W, S, N },  16,   8, 2, 0, "Flame Chomp" },
            { 184, SLOT_AIR, { W, S, N },  16,   4, 2, 0, "Parabomb" },
            {  16, SLOT_AIR, { N, S, N },  16,   8, 2, 1, "Amp" },
            // Water enemies, in the water and nowhere else
            { 194, SLOT_WATER, { N, N, S },  16,   8, 4, 0, "Cheep Cheep" },
            { 195, SLOT_WATER, { N, N, S },  40,  16, 2, 0, "Big Cheep Cheep" },
            { 200, SLOT_WATER, { N, N, S },  16,   8, 3, 0, "Spiny Cheep Cheep" },
            {  95, SLOT_WATER, { N, N, S },  16,   8, 3, 0, "Blooper" },
            {  96, SLOT_WATER, { N, N, S },  16,   8, 2, 0, "Blooper Nanny" },
            { 240, SLOT_WATER, { N, N, S },  16,   8, 2, 1, "Urchin" },
            {  83, SLOT_WATER, { N, N, S },  16,   8, 3, 0, "Fish Bone" },
            {   3, SLOT_WATER, { N, N, W },  40,   8, 1, 0, "Cheep Chomp" },
            // Replacements for chaos mode only
            {  77, SLOT_NONE, { C, C, N },  48,  24, 2, 0, "Thwomp" },
            {  76, SLOT_NONE, { C, C, N },  88,  40, 1, 0, "Big Thwomp" },
            { 118, SLOT_NONE, { C, C, N },  16,   8, 2, 0, "Bullet Bill" },
            { 119, SLOT_NONE, { C, C, N },  48,  16, 1, 0, "Banzai Bill" },
        };
        const uint32_t  kKindCount = sizeof(kKinds) / sizeof(kKinds[0]);

        // Pipe plants only swap within their direction because they are aligned to the pipe
        struct Plant
        {
            uint16_t    id;
            uint8_t     slot;
            const char  *name;
        };

        const Plant kPlants[] =
        {
            { 183, SLOT_PIPE_UP,    "Pipe Piranha Plant" },
            { 171, SLOT_PIPE_UP,    "Pipe Fire Piranha Plant" },
            { 172, SLOT_PIPE_UP,    "Pipe Bone Piranha Plant" },
            { 167, SLOT_PIPE_DOWN,  "Pipe Piranha Plant" },
            { 168, SLOT_PIPE_DOWN,  "Pipe Fire Piranha Plant" },
            { 181, SLOT_PIPE_LEFT,  "Pipe Piranha Plant" },
            { 169, SLOT_PIPE_LEFT,  "Pipe Fire Piranha Plant" },
            { 173, SLOT_PIPE_LEFT,  "Pipe Bone Piranha Plant" },
            { 182, SLOT_PIPE_RIGHT, "Pipe Piranha Plant" },
            { 170, SLOT_PIPE_RIGHT, "Pipe Fire Piranha Plant" },
            { 174, SLOT_PIPE_RIGHT, "Pipe Bone Piranha Plant" },
        };
        const uint32_t  kPlantCount = sizeof(kPlants) / sizeof(kPlants[0]);

        // The pipe enemy generator (68) spawns these, so their models stay loaded
        const uint16_t  kGeneratorSpawns[] = { 135, 29, 136 };

        // Item blocks of the standard tileset (J_Kihon), by object number. Within one row
        // every entry is the same block with a different power-up inside. Coins, beanstalks,
        // trampolines and the Mega Mushroom are not listed and therefore never touched.
        // Blocks that hold a Mini Mushroom are left alone as well, it only shows up as a result.
        struct ItemRow
        {
            uint16_t    fire, leaf, star, oneUp, mini, gold;
        };

        const ItemRow   kItemRows[] =
        {
            {  6, 11,  7,  8, 10, 12 },     // invisible block
            { 20, 21, 24, 26, 23, 22 },     // brick block
            { 32, 33, 37, 38, 35, 34 },     // question block
        };
        const uint16_t  kBrickStarAlt = 25; // second brick block variant holding a star

        uint16_t    g_forced = 0;

        inline uint16_t Read16(const uint8_t *p)
        {
            return static_cast<uint16_t>(p[0] | (p[1] << 8));
        }

        inline uint32_t Read32(const uint8_t *p)
        {
            return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }

        inline void Write16(uint8_t *p, uint16_t v)
        {
            p[0] = static_cast<uint8_t>(v);
            p[1] = static_cast<uint8_t>(v >> 8);
        }

        inline void Write32(uint8_t *p, uint32_t v)
        {
            p[0] = static_cast<uint8_t>(v);
            p[1] = static_cast<uint8_t>(v >> 8);
            p[2] = static_cast<uint8_t>(v >> 16);
            p[3] = static_cast<uint8_t>(v >> 24);
        }

        // Nybbles 4 to 11 are a u32 in bytes 8 to 11, nybbles 12 to 19 one in bytes 12 to 15,
        // highest nybble first (numbering as in CoinKiller)
        inline uint32_t NybbleShift(uint32_t n, uint32_t &byte)
        {
            uint32_t    base = n < 12 ? 8 : 12;
            uint32_t    shift = (7 - (n < 12 ? n - 4 : n - 12)) * 4;

            byte = base + shift / 8;
            return shift % 8;
        }

        inline uint8_t  GetNybble(const uint8_t *spr, uint32_t n)
        {
            uint32_t    byte;
            uint32_t    shift = NybbleShift(n, byte);

            return (spr[byte] >> shift) & 0xF;
        }

        inline void SetNybble(uint8_t *spr, uint32_t n, uint8_t value)
        {
            uint32_t    byte;
            uint32_t    shift = NybbleShift(n, byte);

            spr[byte] = static_cast<uint8_t>((spr[byte] & ~(0xF << shift)) | ((value & 0xF) << shift));
        }

        inline uint32_t Mix(uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7FEB352Du;
            x ^= x >> 15;
            x *= 0x846CA68Bu;
            x ^= x >> 16;
            return x;
        }

        inline uint32_t Hash(uint32_t a, uint32_t b, uint32_t c)
        {
            return Mix(Mix(Mix(a + 0x9E3779B9u) ^ b) ^ c);
        }

        const Kind  *FindKind(uint16_t id)
        {
            for (uint32_t i = 0; i < kKindCount; ++i)
                if (kKinds[i].id == id)
                    return &kKinds[i];
            return nullptr;
        }

        const Plant *FindPlant(uint16_t id)
        {
            for (uint32_t i = 0; i < kPlantCount; ++i)
                if (kPlants[i].id == id)
                    return &kPlants[i];
            return nullptr;
        }

        // Some instances play a special role (empty shell for a puzzle, spawning from
        // a pipe, tied to an event) and are left alone
        bool    CanReplace(const uint8_t *spr, uint16_t id)
        {
            if (spr[6] || spr[7])
                return false;

            switch (id)
            {
                case 165: return GetNybble(spr, 10) == 0 && GetNybble(spr, 9) != 1;
                case 158: return GetNybble(spr, 11) < 2;
                case 95:
                case 96:  return GetNybble(spr, 4) == 0;
                case 135:
                case 136: return GetNybble(spr, 11) == 0;
                case 29:  return GetNybble(spr, 4) == 0;
                case 232: return GetNybble(spr, 11) == 0;
                default:  return true;
            }
        }

        // The original's settings mean nothing to the new kind, so reset them and set
        // only what the new kind needs in this slot
        void    Prepare(uint8_t *spr, uint16_t id, uint32_t slot, uint32_t h)
        {
            std::memset(spr + 6, 0, 10);
            spr[18] = 0;
            spr[19] = 0;

            switch (id)
            {
                case 165:
                    SetNybble(spr, 11, h & 1);                      // green or red
                    break;
                case 185:
                    SetNybble(spr, 11, h & 1);
                    if (slot == SLOT_AIR)
                        SetNybble(spr, 10, 1 + ((h >> 1) & 1));     // flies horizontally or vertically
                    break;
                case 139:
                    SetNybble(spr, 11, 2 + ((h >> 2) % 4));         // two to five Goombas
                    break;
                case 213:
                    SetNybble(spr, 11, (h >> 2) % 3);               // three to five segments
                    break;
                case 175: case 176: case 177: case 178: case 179: case 180:
                    SetNybble(spr, 10, 1);                          // drops to the ground
                    break;
                default:
                    break;
            }
        }

        // Kinds anchor differently: a Hammer Bro is placed a tile above the floor it stands on,
        // a Goomba right on it. Without this a swapped-in Bro would be stuck in the ground.
        int     HeightFix(const Kind &src, const Kind &dst, uint32_t slot)
        {
            if (slot != SLOT_GROUND)
                return src.centre - dst.centre;
            // a Thwomp resting on the floor has nowhere to fall, it hangs four tiles above it
            if (dst.id == 77 || dst.id == 76)
                return src.feet - dst.feet - 64;
            // flyers that end up on a ground slot hover one tile above it
            if (dst.home == SLOT_AIR)
                return src.feet - dst.centre - 24;
            return src.feet - dst.feet;
        }

        // Every kind that may fill this slot in the current mode
        uint32_t    BuildPool(uint32_t slot, const Settings &s, uint16_t exclude, const Kind **pool)
        {
            uint32_t    n = 0;

            if (slot > SLOT_WATER)
                return 0;
            for (uint32_t i = 0; i < kKindCount && n < kMaxPool; ++i)
                if (kKinds[i].id != exclude && kKinds[i].fill[slot] <= s.mode)
                    pool[n++] = &kKinds[i];
            return n;
        }

        // Picks one entry, each with a chance proportional to its weight
        uint32_t    PickWeighted(const Kind *const *pool, uint32_t n, uint32_t h)
        {
            uint32_t    total = 0;

            for (uint32_t i = 0; i < n; ++i)
                total += pool[i]->weight;

            uint32_t    r = h % total;

            for (uint32_t i = 0; i < n; ++i)
            {
                if (r < pool[i]->weight)
                    return i;
                r -= pool[i]->weight;
            }
            return n - 1;
        }

        enum { kMaxPalette = kMaxPool };

        struct Palette
        {
            bool        built;
            uint32_t    count;
            const Kind  *items[kMaxPalette];
        };

        // The kinds an area draws from. The game only has memory for about as many different
        // models as the area was built with: loading 50 kinds where there were 21 corrupts it.
        // So an area gets as many kinds as it had, plus a few, and every new roll brings others.
        void    BuildPalette(Palette &pal, uint32_t slot, const Settings &s, uint32_t levelSeed,
                             uint32_t want)
        {
            const Kind  *pool[kMaxPool];
            uint32_t    n = BuildPool(slot, s, 0, pool);

            if (want < 1)
                want = 1;
            if (want > n)
                want = n;
            for (uint32_t i = 0; i < want; ++i)
            {
                uint32_t    j = i + PickWeighted(pool + i, n - i, Hash(levelSeed, 0x51A7 + slot, i));
                const Kind  *tmp = pool[i];

                pool[i] = pool[j];
                pool[j] = tmp;
                pal.items[i] = pool[i];
            }
            pal.count = want;
            pal.built = true;
        }

        // Something that never moves and cannot be stomped (a plant, an Amp, an Urchin) may only
        // take the place of another such thing. Put where an enemy used to walk or swim, it can
        // seal a narrow passage for good.
        const Kind  *AvoidBlocking(const Kind &src, const Kind *picked, const Kind *const *choices, uint32_t n)
        {
            if (!picked || !picked->fixed || src.fixed)
                return picked;

            uint32_t    at = 0;

            while (at < n && choices[at] != picked)
                ++at;
            for (uint32_t step = 1; step <= n; ++step)
            {
                const Kind  *other = choices[(at + step) % n];

                if (!other->fixed)
                    return other;
            }
            return nullptr;
        }

        void    AddType(uint16_t *types, uint32_t &count, uint16_t id)
        {
            uint32_t    pos = 0;

            while (pos < count && types[pos] < id)
                ++pos;
            if (pos < count && types[pos] == id)
                return;
            if (count >= kMaxTypes)
                return;
            for (uint32_t i = count; i > pos; --i)
                types[i] = types[i - 1];
            types[pos] = id;
            ++count;
        }

        void    Record(Result &res, uint16_t from, uint16_t to)
        {
            for (uint32_t i = 0; i < res.mapCount; ++i)
            {
                if (res.mapFrom[i] == from && res.mapTo[i] == to)
                {
                    ++res.mapHits[i];
                    return;
                }
            }
            if (res.mapCount < MAX_MAP_ENTRIES)
            {
                res.mapFrom[res.mapCount] = from;
                res.mapTo[res.mapCount] = to;
                res.mapHits[res.mapCount] = 1;
                ++res.mapCount;
            }
        }
    }

    void    DefaultSettings(Settings &s)
    {
        s.seed = 1;
        s.enabled = 1;
        s.enemies = 1;
        s.levels = 1;
        s.items = 1;
        s.itemsExtra = 0;
        s.mode = MODE_CHAOS;
        s.perInstance = 1;
        s.variety = 2;
        s.rerollEachTry = 1;
        s.swapGround = 1;
        s.swapAir = 1;
        s.swapWater = 1;
        s.swapPlants = 1;
        s.notify = 0;
        s.writeLog = 1;     // unused by the plugin now, the log is always written
    }

    void    SetForcedTarget(uint16_t id)
    {
        g_forced = id;
    }

    const char  *SpriteName(uint16_t id)
    {
        const Kind  *k = FindKind(id);

        if (k)
            return k->name;

        const Plant *p = FindPlant(id);

        return p ? p->name : nullptr;
    }

    uint32_t    Crc32(const uint8_t *data, uint32_t size)
    {
        uint32_t    crc = 0xFFFFFFFFu;

        for (uint32_t i = 0; i < size; ++i)
        {
            crc ^= data[i];
            for (int b = 0; b < 8; ++b)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
        }
        return ~crc;
    }

    namespace
    {
        bool    ShuffleIn(const uint16_t *list, uint32_t count, uint32_t salt, const Settings &s,
                          uint32_t tries, uint16_t code, uint16_t &out)
        {
            uint16_t    perm[64];
            uint32_t    index = count;

            if (count > 64)
                return false;
            for (uint32_t i = 0; i < count; ++i)
            {
                perm[i] = list[i];
                if (list[i] == code)
                    index = i;
            }
            if (index == count)
                return false;

            if (s.rerollEachTry)
            {
                out = list[Hash(s.seed, 0x1E7E1 + salt, tries * 64 + index) % count];
                return true;
            }

            // one fixed permutation per seed, so every level shows up exactly once
            for (uint32_t k = count - 1; k > 0; --k)
            {
                uint32_t    r = Hash(s.seed, 0x5EED + salt, k) % (k + 1);
                uint16_t    tmp = perm[k];

                perm[k] = perm[r];
                perm[r] = tmp;
            }
            out = perm[index];
            return true;
        }
    }

    bool    ShuffleLevel(const Settings &s, uint32_t tries, uint32_t world, uint32_t number,
                         uint32_t &outWorld, uint32_t &outNumber)
    {
        uint16_t    code = static_cast<uint16_t>((world << 8) | number);
        uint16_t    out = 0;

        if (world > 0xFF || number > 0xFF)
            return false;
        if (!ShuffleIn(kLevelsNormal, kLevelsNormalCount, 0, s, tries, code, out)
            && !ShuffleIn(kLevelsSecret, kLevelsSecretCount, 1, s, tries, code, out))
            return false;

        outWorld = out >> 8;
        outNumber = out & 0xFF;
        return true;
    }

    uint32_t    RandomizeItems(uint8_t *bgdat, uint32_t size, const Settings &s, uint32_t seed)
    {
        const uint32_t  kMaxBlocks = 96;
        uint32_t        changed = 0;

        (void)s;

        // One block type at a time: the power-up blocks of that type trade their contents.
        // A block never receives an item the level did not already have. The game only seems
        // to prepare the items a level was built with, anything else left the block empty.
        for (uint32_t r = 0; r < sizeof(kItemRows) / sizeof(kItemRows[0]); ++r)
        {
            const ItemRow   &row = kItemRows[r];
            const uint16_t  ids[5] = { row.fire, row.leaf, row.star, row.oneUp, row.gold };
            uint32_t        where[kMaxBlocks];
            uint16_t        what[kMaxBlocks];
            uint32_t        n = 0;

            for (uint32_t off = 0; off + kTileObjectSize <= size && n < kMaxBlocks; off += kTileObjectSize)
            {
                uint16_t    id = Read16(bgdat + off);
                bool        match = (r == 1 && id == kBrickStarAlt);

                if (id == 0xFFFF)
                    break;
                // the Mini Mushroom is not in the list: some levels cannot be finished without it
                for (uint32_t i = 0; i < 5 && !match; ++i)
                    match = ids[i] == id;
                if (match)
                {
                    where[n] = off;
                    what[n] = id;
                    ++n;
                }
            }

            for (uint32_t k = n; k > 1; --k)
            {
                uint32_t    j = Hash(seed, 0x17E5 + r, k) % k;
                uint16_t    tmp = what[k - 1];

                what[k - 1] = what[j];
                what[j] = tmp;
            }
            for (uint32_t i = 0; i < n; ++i)
            {
                if (Read16(bgdat + where[i]) != what[i])
                {
                    Write16(bgdat + where[i], what[i]);
                    ++changed;
                }
            }
        }
        return changed;
    }

    uint32_t    CourseSize(const uint8_t *course)
    {
        uint32_t    end = kHeaderSize;

        for (uint32_t i = 0; i < kBlockCount; ++i)
        {
            uint32_t    off = Read32(course + i * 8);
            uint32_t    size = Read32(course + i * 8 + 4);

            if (off < kHeaderSize || off > 0x40000 || size > 0x40000)
                return 0;
            if (off + size > end)
                end = off + size;
        }
        return end;
    }

    bool    RandomizeCourse(const uint8_t *course, uint32_t size, uint8_t *out, uint32_t outCap,
                            const Settings &s, uint32_t levelSeed, uint32_t areaIndex, Result &res)
    {
        std::memset(&res, 0, sizeof(res));

        if (!course || size < kHeaderSize || size > outCap)
            return false;

        uint32_t    sprOff = Read32(course + kBlockSprites * 8);
        uint32_t    sprSize = Read32(course + kBlockSprites * 8 + 4);
        uint32_t    usedOff = Read32(course + kBlockUsed * 8);
        uint32_t    usedSize = Read32(course + kBlockUsed * 8 + 4);

        if (sprOff + sprSize > size || usedOff + usedSize > size)
            return false;

        uint32_t    count = 0;

        while ((count + 1) * kSpriteSize <= sprSize && Read16(course + sprOff + count * kSpriteSize) != 0xFFFF)
            ++count;

        std::memcpy(out, course, size);

        Palette     palettes[SLOT_WATER + 1];
        bool        forcedUsed = false;

        // A checkpoint sends you back to a spot of the level it was taken in. Once the same place
        // on the map can load another level (a new roll, a changed seed), that spot may not exist
        // there, so with random levels the flags go out of reach altogether.
        bool        dropCheckpoints = s.levels != 0;

        // how many different swappable kinds and how many enemies each environment has in this area
        uint32_t    sourceKinds[SLOT_WATER + 1] = { 0, 0, 0 };
        uint32_t    heads[SLOT_WATER + 1] = { 0, 0, 0 };
        uint32_t    extraKinds[SLOT_WATER + 1] = { 0, 0, 0 };
        uint16_t    seen[kMaxPool];
        uint32_t    seenCount = 0;

        for (uint32_t i = 0; i < count; ++i)
        {
            const uint8_t   *spr = out + sprOff + i * kSpriteSize;
            uint16_t        id = Read16(spr);
            const Kind      *k = FindKind(id);
            bool            known = false;

            if (!k || k->home > SLOT_WATER || !CanReplace(spr, id))
                continue;
            ++heads[k->home];
            for (uint32_t n = 0; n < seenCount && !known; ++n)
                known = seen[n] == id;
            if (!known && seenCount < kMaxPool)
            {
                seen[seenCount++] = id;
                ++sourceKinds[k->home];
            }
        }

        // The extra kinds the area can afford go where the enemies are: one at a time to the
        // environment that has the most enemies per kind it may draw from.
        uint32_t    kindsBefore = usedSize / 4;
        uint32_t    budget = kindsBefore + kMinExtraKinds >= kSafeKindTotal ? kMinExtraKinds
                                                                             : kSafeKindTotal - kindsBefore;

        if (budget > kMaxExtraKinds)
            budget = kMaxExtraKinds;
        for (; budget; --budget)
        {
            uint32_t    best = SLOT_COUNT;

            for (uint32_t slot = 0; slot <= SLOT_WATER; ++slot)
            {
                if (!heads[slot])
                    continue;
                // compares heads / (kinds drawn from) without dividing
                if (best == SLOT_COUNT
                    || heads[slot] * (sourceKinds[best] + extraKinds[best])
                       > heads[best] * (sourceKinds[slot] + extraKinds[slot]))
                    best = slot;
            }
            if (best == SLOT_COUNT)
                break;
            ++extraKinds[best];
        }

        std::memset(palettes, 0, sizeof(palettes));
        res.total = static_cast<uint16_t>(count);
        res.typesBefore = static_cast<uint16_t>(usedSize / 4);

        for (uint32_t i = 0; i < count; ++i)
        {
            uint8_t     *spr = out + sprOff + i * kSpriteSize;
            uint16_t    id = Read16(spr);
            uint32_t    h = Hash(levelSeed, (areaIndex << 16) + i, 0xC0FFEE);

            if (id == kCheckpointFlag)
            {
                if (dropCheckpoints)
                {
                    Write16(spr + 4, 0xF000);
                    ++res.checkpoints;
                }
                continue;
            }
            if (!s.enemies)
                continue;

            // While a Gold Ring is active the game draws every enemy with its gold model. An enemy
            // that was swapped in has none, and the game crashes on it. The ring goes out of reach.
            if (id == kGoldRing)
            {
                Write16(spr + 4, 0xF000);
                ++res.rings;
                continue;
            }

            const Plant *plant = FindPlant(id);

            if (plant)
            {
                if (!s.swapPlants)
                    continue;

                // other plants of the same direction, settings are kept
                uint16_t    others[4];
                uint32_t    n = 0;

                for (uint32_t p = 0; p < kPlantCount; ++p)
                    if (kPlants[p].slot == plant->slot && kPlants[p].id != id)
                        others[n++] = kPlants[p].id;
                if (!n)
                    continue;

                // always kind for kind, so pipe plants never add to the number of kinds
                uint16_t    to = others[Hash(levelSeed, 0x9172, id) % n];

                Write16(spr, to);
                ++res.swapped;
                Record(res, id, to);
                continue;
            }

            const Kind  *src = FindKind(id);

            if (!src || src->home == SLOT_NONE || !CanReplace(spr, id))
                continue;

            uint32_t    slot = src->home;

            if ((slot == SLOT_GROUND && !s.swapGround) || (slot == SLOT_AIR && !s.swapAir)
                || (slot == SLOT_WATER && !s.swapWater))
                continue;

            const Kind  *dst = nullptr;

            if (g_forced)
            {
                // testing: the first swappable enemy of the first area becomes the forced kind,
                // everything else stays, so a crash can be pinned on that one kind
                if (areaIndex != 0 || forcedUsed)
                    continue;
                forcedUsed = true;
                dst = FindKind(g_forced);
            }
            else if (s.perInstance)
            {
                Palette &pal = palettes[slot];

                if (!pal.built)
                    BuildPalette(pal, slot, s, levelSeed ^ (areaIndex * 0x9E3779B9u),
                                 sourceKinds[slot] + extraKinds[slot]);
                if (pal.count)
                    dst = AvoidBlocking(*src, pal.items[PickWeighted(pal.items, pal.count, h)],
                                        pal.items, pal.count);
            }
            else
            {
                const Kind  *pool[kMaxPool];
                uint32_t    n = BuildPool(slot, s, id, pool);

                if (n)
                    dst = AvoidBlocking(*src, pool[PickWeighted(pool, n, Hash(levelSeed, 0x7A31, id))], pool, n);
            }

            if (!dst || dst->id == id)
                continue;

            int y = Read16(spr + 4) + HeightFix(*src, *dst, slot);

            Write16(spr, dst->id);
            Write16(spr + 4, static_cast<uint16_t>(y < 0 ? 0 : y));
            Prepare(spr, dst->id, slot, h);
            ++res.swapped;
            Record(res, id, dst->id);
        }

        if (!res.swapped && !res.checkpoints && !res.rings)
            return false;

        // Rebuild the load list: every kind now present, sorted ascending
        uint16_t    types[kMaxTypes];
        uint32_t    typeCount = 0;

        for (uint32_t i = 0; i < count; ++i)
            AddType(types, typeCount, Read16(out + sprOff + i * kSpriteSize));

        for (uint32_t i = 0; i + 4 <= usedSize; i += 4)
        {
            uint16_t    id = Read16(course + usedOff + i);

            for (uint32_t g = 0; g < sizeof(kGeneratorSpawns) / sizeof(kGeneratorSpawns[0]); ++g)
                if (id == kGeneratorSpawns[g])
                    AddType(types, typeCount, id);
        }

        uint32_t    newOff = (size + 3) & ~3u;
        uint32_t    newSize = typeCount * 4;

        if (newOff + newSize > outCap)
            return false;

        std::memset(out + size, 0, newOff - size);
        for (uint32_t i = 0; i < typeCount; ++i)
        {
            Write16(out + newOff + i * 4, types[i]);
            Write16(out + newOff + i * 4 + 2, 0);
        }
        Write32(out + kBlockUsed * 8, newOff);
        Write32(out + kBlockUsed * 8 + 4, newSize);

        res.typesAfter = static_cast<uint16_t>(typeCount);
        res.outSize = newOff + newSize;
        return true;
    }
}
