#include "Randomizer.hpp"

#include <cstring>

#include "LevelTable.hpp"
#include "MemoryTable.hpp"

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
        const uint32_t  kBlockZones = 9;
        const uint32_t  kBlockEntrances = 6;
        const uint32_t  kEntranceSize = 24;     // x and y as u16 in pixels, the zone's ID at +13
        const uint32_t  kZoneSize = 28;     // x, y, width, height as u16 in pixels, the zone's ID at +12
        const uint32_t  kSpriteSize = 24;
        const uint32_t  kMaxTypes = 128;
        const uint32_t  kMaxPool = 64;
        const uint16_t  kCheckpointFlag = 52;
        const uint16_t  kGoldRing = 206;
        // Item rain. The items are made by the plugin while the level runs (see main.cpp), and an
        // item can only be made on the thread of the game. Sprite 252, the item spawner of the
        // Toad Houses, gets it there: the game calls a spawner that is at work once a frame, and
        // the plugin has a hook on that call. So the rain puts spawners of its own into the level,
        // marked with RAIN_MARK, which never throw anything themselves.
        const uint16_t  kItemSpawner = 252;
        // A spawner does nothing until its event (byte 6 of the sprite) is switched on. Sprite 86
        // does that for the event in its own byte 6 as soon as Mario is in the zone it sits in.
        // The rain uses an event number no sprite of the level uses.
        const uint16_t  kZoneEnter = 86;
        const uint32_t  kMaxRain = 40;                  // spawners and controllers per area
        const uint32_t  kMaxZones = 16;
        // The game hands the sprite list out in runs, one per zone number from 0 to 63: it counts
        // the sprites that carry each number and gives zone 0 the first so many, zone 1 the next
        // ones and so on, whatever numbers those sprites really carry (LoadArea of the game).
        const uint32_t  kZoneNumbers = 64;
        const uint32_t  kMaxSpots = 256;
        const uint32_t  kRainCellW = 12 * 16;           // a stretch of the level that gets one spawner
        const uint32_t  kRainCellH = 9 * 16;
        // Spawner and controller come without models, and the items are in memory in every level.
        // All but the Mega Mushroom: it is only there where sprite 288 is on the load list, which
        // does nothing but load it together with Mega Mario. That costs memory like a new kind
        // of enemy does, so the plan of the level decides whether it comes along. Making one
        // where it is not loaded sends the game to address 0.
        const uint16_t  kMegaLoader = 288;
        // Actor memory in KB that new kinds of enemies leave alone when it rains: every item in
        // the air is an actor of its own, some 10 KB, and there can be 48 of them
        const uint16_t  kRainReserve = 480;
        const uint32_t  kMaxLoaded = 256;
        // New kinds a level gets at most, however much memory it has to spare
        const uint32_t  kMaxFreshKinds = 16;
        // What stays untouched in each memory pool, in KB. The table says what a level leaves free
        // right after loading. Enemies and effects that show up while it is played need room as
        // well, and an enemy that was swapped in can be bigger than the one it replaced.
        const uint16_t  kReserve[MEM_COUNT] = { 640, 256, 4, 128, 64, 64 };
        // An enemy the area was not built with is the point of the exercise, so it comes up
        // more often than its weight alone would have it
        const uint32_t  kUnusualBoost = 8;

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
            // flyers above it, swimmers in the water. A fish on land is no fun, and neither is
            // a walker at the bottom of the sea. In Chaos everything on land trades with
            // everything, so a walker may also take the place of a flyer and drop from there.
            // Ground enemies
            { 135, SLOT_GROUND, { S, C, N },  16,   8, 4, 0, "Goomba" },
            { 136, SLOT_GROUND, { S, C, N },  16,   8, 3, 0, "Bone Goomba" },
            { 137, SLOT_GROUND, { W, C, N },  16,  12, 1, 0, "Micro Goomba" },
            { 139, SLOT_GROUND, { S, C, N },  16,   8, 2, 0, "Goomba Tower" },
            { 165, SLOT_GROUND, { S, C, N },  16,   4, 4, 0, "Koopa Troopa" },
            { 158, SLOT_GROUND, { S, C, N },  16,   8, 3, 0, "Buzzy Beetle" },
            { 232, SLOT_GROUND, { S, C, N },  16,   8, 3, 0, "Spiny" },
            { 110, SLOT_GROUND, { S, C, N },  16,   4, 3, 0, "Dry Bones" },
            { 111, SLOT_GROUND, { S, C, N },  48,  24, 1, 0, "Big Dry Bones" },
            {  29, SLOT_GROUND, { S, C, N },  16,   8, 2, 0, "Bob-omb" },
            {  33, SLOT_GROUND, { S, C, N },  32,  16, 2, 0, "Hammer Bro" },
            {  31, SLOT_GROUND, { S, C, N },  32,  16, 2, 0, "Boomerang Bro" },
            {  32, SLOT_GROUND, { S, C, N },  32,  16, 2, 0, "Fire Bro" },
            {  34, SLOT_GROUND, { S, C, N },  48,  24, 1, 0, "Sledge Bro" },
            { 213, SLOT_GROUND, { S, C, N },  16,   0, 2, 0, "Pokey" },
            {  99, SLOT_GROUND, { S, C, N },  16,   4, 2, 0, "Wiggler" },
            { 138, SLOT_GROUND, { S, C, N },  16,   4, 3, 0, "Paragoomba" },
            {  82, SLOT_GROUND, { S, C, N },  16,   8, 2, 0, "Fire Snake" },
            { 159, SLOT_GROUND, { W, C, N },  16,   8, 2, 0, "Spike Top" },
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

        uint16_t    g_forced = 0;
        uint16_t    g_extraKinds[8];
        uint32_t    g_extraKindCount = 0;

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

        bool    HasId(const uint16_t *list, uint32_t count, uint16_t id)
        {
            for (uint32_t i = 0; i < count; ++i)
                if (list[i] == id)
                    return true;
            return false;
        }

        // list: a load list as it sits in a course file, u16 ID and u16 zero per entry
        bool    Listed(const uint8_t *list, uint32_t listSize, uint16_t id)
        {
            for (uint32_t i = 0; i + 4 <= listSize; i += 4)
                if (Read16(list + i) == id)
                    return true;
            return false;
        }

        // The whole level as the plan sees it. The game loads what every area lists when the
        // level starts, so a kind that one area brings along costs nothing in the others.
        struct LevelState
        {
            bool        valid;
            bool        vramPooled;
            Plan        plan;
            uint32_t    areaKey[MAX_AREAS];     // CRC of each course file the plan was made from
            uint16_t    loaded[kMaxLoaded];     // every sprite ID an area of the level lists
            uint32_t    loadedCount;
            uint8_t     rainEvent;              // event number the item rain may use, 0 if none is free
        };

        LevelState  g_level;

        const KindCost  *FindCost(uint16_t id)
        {
            for (uint32_t i = 0; i < kKindCostCount; ++i)
                if (kKindCosts[i].id == id)
                    return &kKindCosts[i];
            return nullptr;
        }

        const LevelRoom *FindRoom(uint32_t key)
        {
            for (uint32_t i = 0; i < kLevelRoomCount; ++i)
                if (kLevelRooms[i].key == key)
                    return &kLevelRooms[i];
            return nullptr;
        }

        // Loaded by the level as it is, or added by the plan
        bool    Available(uint16_t id)
        {
            return HasId(g_level.loaded, g_level.loadedCount, id)
                || HasId(g_level.plan.fresh, g_level.plan.freshCount, id);
        }

        // The environment an enemy of the original level belongs to, SLOT_COUNT if it stays
        uint32_t    SourceSlot(const uint8_t *spr, uint16_t id, const Settings &s)
        {
            const Plant *plant = FindPlant(id);

            if (plant)
                return s.swapPlants ? plant->slot : static_cast<uint32_t>(SLOT_COUNT);

            const Kind  *k = FindKind(id);

            if (!k || k->home > SLOT_WATER || !CanReplace(spr, id))
                return SLOT_COUNT;
            if ((k->home == SLOT_GROUND && !s.swapGround) || (k->home == SLOT_AIR && !s.swapAir)
                || (k->home == SLOT_WATER && !s.swapWater))
                return SLOT_COUNT;
            return k->home;
        }

        // May this kind stand in this slot under the current settings
        bool    Fills(uint16_t id, uint32_t slot, const Settings &s)
        {
            if (slot > SLOT_WATER)
            {
                const Plant *plant = FindPlant(id);

                return plant && plant->slot == slot;
            }

            const Kind  *k = FindKind(id);

            return k && k->fill[slot] <= s.mode;
        }

        enum { kMaxPalette = kMaxPool };

        struct Palette
        {
            bool        built;
            uint32_t    count;
            uint32_t    total;                  // sum of the weights
            const Kind  *items[kMaxPalette];
            uint16_t    weights[kMaxPalette];
        };

        // The kinds an area draws from: what the level loads anyway and what the plan added.
        // own is the load list the area came with, kinds that are not on it count as unusual.
        void    BuildPalette(Palette &pal, uint32_t slot, const Settings &s, const uint8_t *own, uint32_t ownSize,
                             bool usePlan)
        {
            pal.count = 0;
            pal.total = 0;
            pal.built = true;
            for (uint32_t i = 0; i < kKindCount && pal.count < kMaxPalette; ++i)
            {
                const Kind  &k = kKinds[i];
                bool        usual;

                if (k.fill[slot] > s.mode)
                    continue;
                usual = Listed(own, ownSize, k.id);
                if (!usual && !(usePlan && Available(k.id)))
                    continue;
                pal.items[pal.count] = &k;
                pal.weights[pal.count] = static_cast<uint16_t>(k.weight * (usual ? 1 : kUnusualBoost));
                pal.total += pal.weights[pal.count];
                ++pal.count;
            }
        }

        const Kind  *PickFrom(const Palette &pal, uint32_t h)
        {
            if (!pal.count || !pal.total)
                return nullptr;

            uint32_t    r = h % pal.total;

            for (uint32_t i = 0; i < pal.count; ++i)
            {
                if (r < pal.weights[i])
                    return pal.items[i];
                r -= pal.weights[i];
            }
            return pal.items[pal.count - 1];
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
        s.mode = MODE_CHAOS;
        s.perInstance = 1;
        s.castles = 1;
        s.rerollEachTry = 1;
        s.swapGround = 1;
        s.swapAir = 1;
        s.swapWater = 1;
        s.swapPlants = 1;
        s.notify = 0;
        s.autosave = 1;
        s.noRepeats = 0;
        s.itemRain = 0;
    }

    void    AddExtraKind(uint16_t id)
    {
        if (!id)
            g_extraKindCount = 0;
        else if (g_extraKindCount < 8)
            g_extraKinds[g_extraKindCount++] = id;
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
        bool    g_dropSecretExit = false;
        bool    g_nodeShuffled = false;

        bool    Contains(const uint16_t *list, uint32_t count, uint16_t code)
        {
            for (uint32_t i = 0; i < count; ++i)
                if (list[i] == code)
                    return true;
            return false;
        }

        // One fixed permutation of a class per seed, so every level shows up exactly once
        uint16_t    Permuted(const uint16_t *list, uint32_t count, uint32_t salt, uint32_t seed, uint16_t code)
        {
            uint16_t    perm[64];
            uint32_t    index = 0;

            if (count > 64)
                return code;
            for (uint32_t i = 0; i < count; ++i)
            {
                perm[i] = list[i];
                if (list[i] == code)
                    index = i;
            }
            for (uint32_t k = count - 1; k > 0; --k)
            {
                uint32_t    r = Hash(seed, 0x5EED + salt, k) % (k + 1);
                uint16_t    tmp = perm[k];

                perm[k] = perm[r];
                perm[r] = tmp;
            }
            return perm[index];
        }
    }

    bool    DropSecretExit(void)
    {
        return g_dropSecretExit;
    }

    void    SetNodeShuffled(bool shuffled)
    {
        g_nodeShuffled = shuffled;
    }

    namespace
    {
        // The four kinds of levels that trade places, each only within itself. A level's slot is
        // its position counted through all four lists, that is what Progress keeps a bit for.
        struct LevelClass
        {
            const uint16_t  *list;
            uint32_t        count;
            uint32_t        firstSlot;
        };

        const LevelClass    kClasses[] =
        {
            { kLevelsNormal, kLevelsNormalCount, 0 },
            { kLevelsSecret, kLevelsSecretCount, kLevelsNormalCount },
            { kLevelsCastle, kLevelsCastleCount, kLevelsNormalCount + kLevelsSecretCount },
            { kLevelsTower, kLevelsTowerCount, kLevelsNormalCount + kLevelsSecretCount + kLevelsCastleCount },
            { kLevelsTowerSecret, kLevelsTowerSecretCount,
              kLevelsNormalCount + kLevelsSecretCount + kLevelsCastleCount + kLevelsTowerCount },
        };
        const uint32_t      kClassCount = sizeof(kClasses) / sizeof(kClasses[0]);

        Progress    *g_progress = nullptr;
        bool        g_progressReset = false;

        const LevelClass    *ClassOf(uint16_t code, uint32_t &index)
        {
            for (uint32_t c = 0; c < kClassCount; ++c)
                for (uint32_t i = 0; i < kClasses[c].count; ++i)
                    if (kClasses[c].list[i] == code)
                    {
                        index = i;
                        return &kClasses[c];
                    }
            return nullptr;
        }

        bool    DoneSlot(const Progress &p, uint32_t slot)
        {
            return slot < LEVEL_SLOTS && (p.done[slot / 8] >> (slot % 8)) & 1;
        }
    }

    int     LevelSlot(uint16_t code)
    {
        uint32_t            index = 0;
        const LevelClass    *c = ClassOf(code, index);

        return c ? static_cast<int>(c->firstSlot + index) : -1;
    }

    bool    IsDone(const Progress &p, uint16_t code)
    {
        int slot = LevelSlot(code);

        return slot >= 0 && DoneSlot(p, static_cast<uint32_t>(slot));
    }

    void    MarkDone(Progress &p, uint16_t code)
    {
        int slot = LevelSlot(code);

        if (slot >= 0 && slot < LEVEL_SLOTS)
            p.done[slot / 8] = static_cast<uint8_t>(p.done[slot / 8] | (1 << (slot % 8)));
    }

    uint32_t    LeftOfItsKind(const Progress &p, uint16_t code)
    {
        uint32_t            index = 0;
        const LevelClass    *c = ClassOf(code, index);
        uint32_t            left = 0;

        for (uint32_t i = 0; c && i < c->count; ++i)
            left += !DoneSlot(p, c->firstSlot + i);
        return left;
    }

    void    SetProgress(Progress *p)
    {
        g_progress = p;
    }

    bool    ProgressReset(void)
    {
        return g_progressReset;
    }

    bool    ShuffleLevel(const Settings &s, uint32_t tries, uint32_t world, uint32_t number, uint32_t lastGiven,
                         uint32_t &outWorld, uint32_t &outNumber)
    {
        uint16_t            code = static_cast<uint16_t>((world << 8) | number);
        uint32_t            own = 0;
        const LevelClass    *c = world <= 0xFF && number <= 0xFF ? ClassOf(code, own) : nullptr;
        uint16_t            out = code;

        g_dropSecretExit = false;
        g_progressReset = false;

        // Levels only trade places within their kind: ordinary levels, levels with a secret exit
        // (so every secret path on the map has such a level on it), the castles of the
        // Koopalings, the towers without and the towers with a secret exit. The castles of
        // Bowser and everything else stay put.
        if (!c)
            return false;
        if ((c->list == kLevelsCastle || c->list == kLevelsTower || c->list == kLevelsTowerSecret) && !s.castles)
            return false;

        if (!s.rerollEachTry)
        {
            // the seed alone decides: one fixed permutation of each kind
            out = Permuted(c->list, c->count, c->firstSlot, s.seed, code);
        }
        else
        {
            // A new roll on every visit. The level that belongs to the place is skipped, that
            // would not feel shuffled, and so is the one that came up last. With a list of
            // finished levels those are skipped as well, until the kind has run out of others,
            // then it starts over.
            uint32_t    h = Hash(s.seed, 0x1E7E1 + c->firstSlot, tries);
            bool        found = false;

            for (uint32_t pass = 0; pass < 2 && !found; ++pass)
            {
                bool    fresh = pass == 0 && g_progress != nullptr;

                if (pass == 0 && !fresh)
                    continue;
                for (uint32_t step = 0; step < c->count && !found; ++step)
                {
                    uint32_t    i = (h + step) % c->count;

                    out = c->list[i];
                    found = out != lastGiven && out != code && !(fresh && DoneSlot(*g_progress, c->firstSlot + i));
                }
                if (!found && fresh)
                {
                    // every other level of this kind is finished: wipe the marks of the kind
                    for (uint32_t i = 0; i < c->count; ++i)
                    {
                        uint32_t    slot = c->firstSlot + i;

                        g_progress->done[slot / 8] = static_cast<uint8_t>(g_progress->done[slot / 8] & ~(1 << (slot % 8)));
                    }
                    g_progressReset = true;
                }
            }
            if (!found)
                out = c->list[(h + 1) % c->count];
        }

        // A secret exit taken where the map has no secret path leads nowhere. Levels stay within
        // their kind, so this cannot come up any more, the check stays as a safety net.
        g_dropSecretExit = c->list == kLevelsNormal && Contains(kLevelsSecret, kLevelsSecretCount, out);
        outWorld = out >> 8;
        outNumber = out & 0xFF;
        return true;
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

    const Plan  &LastPlan(void)
    {
        return g_level.plan;
    }

    void    PlanLevel(const uint8_t *const *courses, const uint32_t *sizes, const Settings &s,
                      uint32_t levelSeed, bool vramPooled)
    {
        LevelState  &lv = g_level;
        uint32_t    heads[SLOT_COUNT];
        uint32_t    fixedHeads[SLOT_COUNT];     // those of them that never move, see AvoidBlocking
        uint64_t    usedEvents = 1;             // event numbers the sprites of the level refer to, 0 means none

        std::memset(&lv, 0, sizeof(lv));
        std::memset(heads, 0, sizeof(heads));
        std::memset(fixedHeads, 0, sizeof(fixedHeads));
        lv.valid = true;
        lv.vramPooled = vramPooled;

        for (uint32_t a = 0; a < MAX_AREAS; ++a)
        {
            const uint8_t   *course = courses[a];
            uint32_t        size = sizes[a];

            if (!course || size < kHeaderSize)
                continue;

            uint32_t    sprOff = Read32(course + kBlockSprites * 8);
            uint32_t    sprSize = Read32(course + kBlockSprites * 8 + 4);
            uint32_t    usedOff = Read32(course + kBlockUsed * 8);
            uint32_t    usedSize = Read32(course + kBlockUsed * 8 + 4);

            if (sprOff + sprSize > size || usedOff + usedSize > size)
                continue;
            lv.areaKey[a] = Crc32(course, size);
            for (uint32_t i = 0; i + 4 <= usedSize; i += 4)
            {
                uint16_t    id = Read16(course + usedOff + i);

                if (!HasId(lv.loaded, lv.loadedCount, id) && lv.loadedCount < kMaxLoaded)
                    lv.loaded[lv.loadedCount++] = id;
            }
            for (uint32_t i = 0; (i + 1) * kSpriteSize <= sprSize; ++i)
            {
                const uint8_t   *spr = course + sprOff + i * kSpriteSize;
                uint16_t        id = Read16(spr);

                if (id == 0xFFFF)
                    break;

                // bytes 6 and 7 are where a sprite names the events it waits for or sets off
                if (spr[6] < 64)
                    usedEvents |= 1ull << spr[6];
                if (spr[7] < 64)
                    usedEvents |= 1ull << spr[7];

                uint32_t    slot = SourceSlot(spr, id, s);

                if (slot < SLOT_COUNT)
                {
                    const Kind  *k = FindKind(id);

                    ++heads[slot];
                    if (k && k->fixed)
                        ++fixedHeads[slot];
                }
            }
        }
        lv.plan.key = lv.areaKey[0];
        for (uint32_t e = 63; e >= 32 && !lv.rainEvent; --e)
            if (!((usedEvents >> e) & 1))
                lv.rainEvent = static_cast<uint8_t>(e);

        // The item rain needs nothing loaded, only an event number of its own. A level that
        // loads the Mega Mushroom anyway has it in its rain for free.
        if (s.itemRain && lv.rainEvent)
        {
            lv.plan.rain = 1;
            lv.plan.mega = HasId(lv.loaded, lv.loadedCount, kMegaLoader);
        }

        // A level that is not in the table keeps to the kinds it has, those cost nothing
        const LevelRoom *room = FindRoom(lv.plan.key);

        if (!room || (!s.enemies && !lv.plan.rain))
            return;
        lv.plan.known = 1;

        uint32_t    left[MEM_COUNT];
        uint32_t    spent[MEM_COUNT];

        for (uint32_t r = 0; r < MEM_COUNT; ++r)
        {
            lv.plan.freeBefore[r] = room->free[r];
            left[r] = room->free[r] > kReserve[r] ? room->free[r] - kReserve[r] : 0;
            spent[r] = 0;
        }

        // with pooling one bank takes what the other cannot hold, so only the sum counts
        uint32_t    vramReserve = kReserve[MEM_VRAM_A] + kReserve[MEM_VRAM_B];
        uint32_t    vramLeft = room->both > vramReserve ? room->both - vramReserve : 0;

        if (lv.plan.rain)
            left[MEM_ACTOR] = left[MEM_ACTOR] > kRainReserve ? left[MEM_ACTOR] - kRainReserve : 0;

        // The Mega Mushroom of the item rain comes first if the level has room for what it
        // loads. Without that room everything else rains.
        if (lv.plan.rain && !lv.plan.mega)
        {
            const KindCost  *cost = FindCost(kMegaLoader);
            bool            fits = cost != nullptr;

            for (uint32_t r = 0; fits && r < MEM_VRAM_A; ++r)
                fits = cost->cost[r] <= left[r];
            if (fits && vramPooled)
                fits = static_cast<uint32_t>(cost->cost[MEM_VRAM_A]) + cost->cost[MEM_VRAM_B] <= vramLeft;
            else if (fits)
                fits = cost->cost[MEM_VRAM_A] <= left[MEM_VRAM_A] && cost->cost[MEM_VRAM_B] <= left[MEM_VRAM_B];
            if (fits)
            {
                lv.plan.mega = 1;
                for (uint32_t m = 0; m < MEM_COUNT; ++m)
                {
                    spent[m] += cost->cost[m];
                    left[m] = left[m] > cost->cost[m] ? left[m] - cost->cost[m] : 0;
                }
                vramLeft -= vramPooled ? cost->cost[MEM_VRAM_A] + cost->cost[MEM_VRAM_B] : 0;
            }
        }
        bool        closed[SLOT_COUNT];

        std::memset(closed, 0, sizeof(closed));
        while (s.enemies && lv.plan.freshCount < kMaxFreshKinds && lv.plan.freshCount < MAX_FRESH)
        {
            // The next new kind goes to the environment with the most enemies per kind it can
            // draw on. Compared without dividing.
            uint32_t    best = SLOT_COUNT;
            uint32_t    bestChoices = 0;

            for (uint32_t slot = 0; slot < SLOT_COUNT; ++slot)
            {
                if (!heads[slot] || closed[slot])
                    continue;

                uint32_t    choices = 1;

                if (slot <= SLOT_WATER)
                {
                    for (uint32_t i = 0; i < kKindCount; ++i)
                        choices += kKinds[i].fill[slot] <= s.mode && Available(kKinds[i].id);
                }
                else
                {
                    for (uint32_t i = 0; i < kPlantCount; ++i)
                        choices += kPlants[i].slot == slot && Available(kPlants[i].id);
                }
                if (best == SLOT_COUNT || heads[slot] * bestChoices > heads[best] * choices)
                {
                    best = slot;
                    bestChoices = choices;
                }
            }
            if (best == SLOT_COUNT)
                break;

            // what could come in for that environment and still fits into every pool
            uint16_t    ids[kMaxPool];
            uint16_t    weights[kMaxPool];
            uint32_t    n = 0;
            uint32_t    total = 0;

            for (uint32_t i = 0; i < kKindCount + kPlantCount && n < kMaxPool; ++i)
            {
                uint16_t        id = i < kKindCount ? kKinds[i].id : kPlants[i - kKindCount].id;
                const KindCost  *cost = FindCost(id);
                bool            fits = cost != nullptr && Fills(id, best, s) && !Available(id);

                // something that never moves only ever replaces its like, without one of those
                // in the level it would be loaded for nothing
                if (fits && i < kKindCount && kKinds[i].fixed && !fixedHeads[best])
                    fits = false;

                for (uint32_t r = 0; fits && r < MEM_VRAM_A; ++r)
                    fits = cost->cost[r] <= left[r];
                if (fits && vramPooled)
                    fits = static_cast<uint32_t>(cost->cost[MEM_VRAM_A]) + cost->cost[MEM_VRAM_B] <= vramLeft;
                else if (fits)
                    fits = cost->cost[MEM_VRAM_A] <= left[MEM_VRAM_A] && cost->cost[MEM_VRAM_B] <= left[MEM_VRAM_B];
                if (!fits)
                    continue;
                ids[n] = id;
                weights[n] = i < kKindCount ? kKinds[i].weight : 1;
                total += weights[n];
                ++n;
            }
            if (!n)
            {
                closed[best] = true;
                continue;
            }

            uint32_t    r = Hash(levelSeed, 0x51A7 + best, lv.plan.freshCount) % total;
            uint32_t    pick = 0;

            while (pick + 1 < n && r >= weights[pick])
                r -= weights[pick++];

            const KindCost  *cost = FindCost(ids[pick]);

            lv.plan.fresh[lv.plan.freshCount++] = ids[pick];
            for (uint32_t m = 0; m < MEM_COUNT; ++m)
            {
                spent[m] += cost->cost[m];
                left[m] = left[m] > cost->cost[m] ? left[m] - cost->cost[m] : 0;
            }
            vramLeft -= vramPooled ? cost->cost[MEM_VRAM_A] + cost->cost[MEM_VRAM_B] : 0;
        }

        for (uint32_t r = 0; r < MEM_COUNT; ++r)
            lv.plan.freeAfter[r] = static_cast<uint16_t>(room->free[r] > spent[r] ? room->free[r] - spent[r] : 0);
        if (vramPooled)
        {
            // what one bank cannot hold comes out of the other
            for (uint32_t r = MEM_VRAM_A; r <= MEM_VRAM_B; ++r)
            {
                uint32_t    other = r == MEM_VRAM_A ? MEM_VRAM_B : MEM_VRAM_A;
                uint32_t    over = spent[r] > room->free[r] ? spent[r] - room->free[r] : 0;

                lv.plan.freeAfter[other] = static_cast<uint16_t>(lv.plan.freeAfter[other] > over
                                                                 ? lv.plan.freeAfter[other] - over : 0);
            }
        }
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
        bool        touched = false;

        // The plan only counts if it was made for this very level. Without one the area keeps
        // to the kinds on its own load list, which cost nothing.
        bool        usePlan = g_level.valid && areaIndex < MAX_AREAS
                           && (g_level.areaKey[areaIndex] == Crc32(course, size)
                               || (areaIndex > 0 && g_level.areaKey[areaIndex] == 0));

        // A checkpoint sends you back to a spot of the level it was taken in. Once the same place
        // on the map can load another level (a new roll, a changed seed), that spot may not exist
        // there, so the flags go out of reach wherever levels are shuffled. Towers and whatever
        // else stays put keep theirs.
        bool        dropCheckpoints = s.levels != 0 && g_nodeShuffled;

        std::memset(palettes, 0, sizeof(palettes));
        res.total = static_cast<uint16_t>(count);
        res.typesBefore = static_cast<uint16_t>(usedSize / 4);

        for (uint32_t i = 0; i < count; ++i)
        {
            uint8_t     *spr = out + sprOff + i * kSpriteSize;
            uint16_t    id = Read16(spr);
            uint32_t    h = Hash(levelSeed, (areaIndex << 16) + i, 0xC0FFEE);

            // Goal pole with the red flag: nybble 4 marks the secret exit. Where the map has no
            // secret path it turns into an ordinary goal, so the room it stands in still has
            // a way out.
            if (id == 97 && g_dropSecretExit && (spr[11] >> 4))
            {
                spr[11] &= 0x0F;
                touched = true;
                continue;
            }
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

                // other plants of the same direction that the level has loaded, settings are kept
                uint16_t    others[4];
                uint32_t    n = 0;

                for (uint32_t p = 0; p < kPlantCount && n < 4; ++p)
                    if (kPlants[p].slot == plant->slot && kPlants[p].id != id
                        && ((usePlan && Available(kPlants[p].id))
                            || Listed(course + usedOff, usedSize, kPlants[p].id)))
                        others[n++] = kPlants[p].id;
                if (!n)
                    continue;

                // always kind for kind and the same in every area of the level
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
            else
            {
                Palette &pal = palettes[slot];

                if (!pal.built)
                    BuildPalette(pal, slot, s, course + usedOff, usedSize, usePlan);
                // every enemy on its own, or every enemy of a kind the same way
                dst = AvoidBlocking(*src, PickFrom(pal, s.perInstance ? h : Hash(levelSeed, 0x7A31, id)),
                                    pal.items, pal.count);
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

        // Item rain: what it adds to the sprites of the area. A sprite is only awake near the
        // camera, so the level is cut into stretches and every stretch with something in it gets
        // a spawner, right where that thing stands: wherever the level has anything to show,
        // one of them is awake and the plugin gets its turn.
        static uint8_t  rain[kMaxRain][kSpriteSize];
        uint32_t        rainCount = 0;
        uint32_t        zoneOff = Read32(course + kBlockZones * 8);
        uint32_t        zoneSize = Read32(course + kBlockZones * 8 + 4);
        bool            mega = usePlan && g_level.plan.mega;

        if (s.itemRain && usePlan && g_level.plan.rain && zoneOff + zoneSize <= size)
        {
            // one controller per zone switches the spawners of that zone on
            uint8_t     zoneIds[kMaxZones];
            uint32_t    zones = 0;

            for (uint32_t z = 0; (z + 1) * kZoneSize <= zoneSize && zones < kMaxZones && rainCount < kMaxRain; ++z)
            {
                const uint8_t   *zone = course + zoneOff + z * kZoneSize;

                if (zone[12] >= kZoneNumbers)
                    continue;

                uint8_t         *ctrl = rain[rainCount++];

                zoneIds[zones++] = zone[12];
                std::memset(ctrl, 0, kSpriteSize);
                Write16(ctrl, kZoneEnter);
                Write16(ctrl + 2, static_cast<uint16_t>(Read16(zone) + 32));
                Write16(ctrl + 4, static_cast<uint16_t>(Read16(zone + 2) + 32));
                ctrl[6] = g_level.rainEvent;
                ctrl[16] = zone[12];
            }

            struct Spot
            {
                uint16_t    x, y;
                uint8_t     zone;
            };
            static Spot spots[kMaxSpots];
            uint32_t    spotCount = 0;

            auto        note = [&](uint32_t x, uint32_t y, uint8_t zone)
            {
                uint32_t    z = 0;

                // without a zone there is no controller, the spawner would never start
                while (z < zones && zoneIds[z] != zone)
                    ++z;
                if (z == zones)
                    return;
                for (uint32_t i = 0; i < spotCount; ++i)
                    if (spots[i].zone == zone && spots[i].x / kRainCellW == x / kRainCellW
                        && spots[i].y / kRainCellH == y / kRainCellH)
                        return;
                if (spotCount < kMaxSpots)
                {
                    spots[spotCount].x = static_cast<uint16_t>(x);
                    spots[spotCount].y = static_cast<uint16_t>(y);
                    spots[spotCount++].zone = zone;
                }
            };

            uint32_t    entOff = Read32(course + kBlockEntrances * 8);
            uint32_t    entSize = Read32(course + kBlockEntrances * 8 + 4);

            // entrances first: where Mario comes in, the rain should be awake for sure
            for (uint32_t e = 0; entOff + entSize <= size && (e + 1) * kEntranceSize <= entSize; ++e)
            {
                const uint8_t   *ent = course + entOff + e * kEntranceSize;

                note(Read16(ent), Read16(ent + 2), ent[13]);
            }
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint8_t   *spr = course + sprOff + i * kSpriteSize;

                note(Read16(spr + 2), Read16(spr + 4), spr[16]);
            }

            // in the order of the level, so that leaving some out thins them evenly
            for (uint32_t i = 1; i < spotCount; ++i)
            {
                Spot        spot = spots[i];
                uint32_t    key = (static_cast<uint32_t>(spot.zone) << 24) | (static_cast<uint32_t>(spot.x) << 8) | (spot.y >> 8);
                uint32_t    j = i;

                while (j > 0 && ((static_cast<uint32_t>(spots[j - 1].zone) << 24)
                                 | (static_cast<uint32_t>(spots[j - 1].x) << 8) | (spots[j - 1].y >> 8)) > key)
                {
                    spots[j] = spots[j - 1];
                    --j;
                }
                spots[j] = spot;
            }

            uint32_t    room = kMaxRain - rainCount;
            uint32_t    use = spotCount < room ? spotCount : room;

            for (uint32_t k = 0; k < use; ++k)
            {
                const Spot  &spot = spots[spotCount <= room ? k : k * spotCount / room];
                uint8_t     *spr = rain[rainCount++];

                std::memset(spr, 0, kSpriteSize);
                Write16(spr, kItemSpawner);
                Write16(spr + 2, spot.x);
                Write16(spr + 4, spot.y);
                spr[6] = g_level.rainEvent;
                // Settings as a Toad House has them, a mushroom, thrown, fifteen of them. They
                // never come to anything: the mark makes the plugin keep the spawner from throwing.
                spr[11] = static_cast<uint8_t>((1 << 4) | RAIN_MARK);
                spr[10] = static_cast<uint8_t>((1 << 4) | 3);
                spr[9] = static_cast<uint8_t>((15 << 4) | 15);
                spr[16] = spot.zone;
            }
        }

        if (!res.swapped && !res.checkpoints && !res.rings && !touched && !rainCount
            && !(g_extraKindCount && areaIndex == 0))
            return false;

        // The new load list is the old one plus whatever came in. Nothing is ever taken off it:
        // other things in the level can depend on what a kind loads, and the memory budget of
        // the plan is measured against the level as it was built.
        uint16_t    types[kMaxTypes];
        uint32_t    typeCount = 0;

        for (uint32_t i = 0; i + 4 <= usedSize; i += 4)
            AddType(types, typeCount, Read16(course + usedOff + i));
        for (uint32_t i = 0; i < count; ++i)
            AddType(types, typeCount, Read16(out + sprOff + i * kSpriteSize));
        // testing: kinds that are loaded without standing anywhere, to measure what one costs
        for (uint32_t i = 0; areaIndex == 0 && i < g_extraKindCount; ++i)
            AddType(types, typeCount, g_extraKinds[i]);

        uint32_t    newOff = (size + 3) & ~3u;
        uint32_t    newSize = typeCount * 4;

        if (newOff + newSize > outCap)
            return false;
        std::memset(out + size, 0, newOff - size);

        // The spawners need a longer sprite list. It goes behind the file as well: the sprites
        // as they are now, the spawners, and the end mark the old list closed with.
        uint32_t    tail = sprSize - count * kSpriteSize;
        uint32_t    rainSize = (count + rainCount) * kSpriteSize + tail;

        if (rainCount && newOff + ((rainSize + 3) & ~3u) + newSize + 12 <= outCap)
        {
            // Every sprite of the rain goes to the end of the run of its zone. Behind the whole
            // list they would make the runs longer and shift every later one: sprites would turn
            // up in the wrong zone or not at all, a tower would stand there without its water.
            uint32_t    perZone[kZoneNumbers];
            uint8_t     *dst = out + newOff;
            uint32_t    src = 0;

            std::memset(perZone, 0, sizeof(perZone));
            for (uint32_t i = 0; i < count; ++i)
            {
                uint8_t zone = out[sprOff + i * kSpriteSize + 16];

                if (zone < kZoneNumbers)
                    ++perZone[zone];
            }
            for (uint32_t z = 0; z < kZoneNumbers; ++z)
            {
                std::memcpy(dst, out + sprOff + src * kSpriteSize, perZone[z] * kSpriteSize);
                dst += perZone[z] * kSpriteSize;
                src += perZone[z];
                for (uint32_t r = 0; r < rainCount; ++r)
                {
                    if (rain[r][16] != z)
                        continue;
                    std::memcpy(dst, rain[r], kSpriteSize);
                    dst += kSpriteSize;
                }
            }
            // what the game gives to no zone, and the end mark the old list closed with
            std::memcpy(dst, out + sprOff + src * kSpriteSize, (count - src) * kSpriteSize);
            dst += (count - src) * kSpriteSize;
            std::memcpy(dst, course + sprOff + count * kSpriteSize, tail);
            Write32(out + kBlockSprites * 8, newOff);
            Write32(out + kBlockSprites * 8 + 4, rainSize);
            res.rain = static_cast<uint16_t>(rainCount);
            AddType(types, typeCount, kItemSpawner);
            AddType(types, typeCount, kZoneEnter);
            if (mega)
                AddType(types, typeCount, kMegaLoader);
            newSize = typeCount * 4;
            newOff += rainSize;
            std::memset(out + newOff, 0, ((newOff + 3) & ~3u) - newOff);
            newOff = (newOff + 3) & ~3u;
        }
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
