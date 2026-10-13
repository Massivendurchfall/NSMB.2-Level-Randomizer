// Runs the randomizer core on a PC against every course file of the game.
// Input: the file written by tools/make_blob.py, every course with its level and area.
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "LevelTable.hpp"
#include "MemoryTable.hpp"
#include "Randomizer.hpp"

static uint32_t R32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t R16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static int g_errors = 0;

static bool OneOf(uint16_t id, const uint16_t *list, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (list[i] == id)
            return true;
    return false;
}

static bool IsSwimmer(uint16_t id)
{
    static const uint16_t ids[] = { 194, 195, 200, 95, 96, 240, 83, 3 };
    return OneOf(id, ids, sizeof(ids) / sizeof(ids[0]));
}

static bool IsFixed(uint16_t id)
{
    // grounded and pipe Piranha Plants, Amp, Urchin
    static const uint16_t ids[] = { 175, 176, 177, 178, 179, 180, 16, 240,
                                    183, 171, 172, 167, 168, 181, 169, 173, 182, 170, 174 };
    return OneOf(id, ids, sizeof(ids) / sizeof(ids[0]));
}

static void Fail(size_t course, const char *what)
{
    if (++g_errors <= 20)
        std::printf("ERROR course %zu: %s\n", course, what);
}

static void Check(size_t index, const std::vector<uint8_t> &in, const uint8_t *out, const Rando::Result &res)
{
    for (int b = 0; b < 17; ++b)
    {
        uint32_t off = R32(&in[b * 8]), size = R32(&in[b * 8 + 4]);

        // the load list always moves behind the file, the sprite list does with the item rain
        if (b == 8 || (b == 7 && res.rain))
            continue;
        if (R32(out + b * 8) != off || R32(out + b * 8 + 4) != size)
            Fail(index, "header of an unrelated block changed");
        if (b != 7 && std::memcmp(&in[off], out + off, size) != 0)
            Fail(index, "contents of an unrelated block changed");
    }

    uint32_t sOff = R32(&in[7 * 8]), sSize = R32(&in[7 * 8 + 4]);
    uint32_t nOff = R32(out + 7 * 8), nSize = R32(out + 7 * 8 + 4);     // where the sprites are now
    uint32_t uOff = R32(out + 8 * 8), uSize = R32(out + 8 * 8 + 4);

    if (!res.rain && (nOff != sOff || nSize != sSize))
        Fail(index, "sprite list moved without item rain");
    if (res.rain && nSize != sSize + res.rain * 24u)
        Fail(index, "sprite list with item rain has the wrong size");
    uint32_t count = 0, changed = 0, flags = 0;

    if (uOff + uSize != res.outSize)
        Fail(index, "load list is not at the end");
    // nothing the area loaded before may be missing afterwards
    uint32_t oOff = R32(&in[8 * 8]), oSize = R32(&in[8 * 8 + 4]);

    for (uint32_t i = 0; i + 4 <= oSize; i += 4)
    {
        bool kept = false;

        for (uint32_t k = 0; k + 4 <= uSize && !kept; k += 4)
            kept = R16(out + uOff + k) == R16(&in[oOff + i]);
        if (!kept)
            Fail(index, "a kind was dropped from the load list");
    }
    for (uint32_t i = 4; i < uSize; i += 4)
        if (R16(out + uOff + i) <= R16(out + uOff + i - 4))
            Fail(index, "load list is not ascending");

    while ((count + 1) * 24 <= sSize && R16(&in[sOff + count * 24]) != 0xFFFF)
    {
        const uint8_t *a = &in[sOff + count * 24];
        const uint8_t *b = out + nOff + count * 24;
        uint16_t id = R16(b);
        bool listed = false;

        // a swap may correct the height, everything else about the placement stays
        if (R16(a + 2) != R16(b + 2) || a[16] != b[16] || a[17] != b[17])
            Fail(index, "x position, zone or layer changed");
        if (id != R16(a))
        {
            int dy = (int)R16(b + 4) - (int)R16(a + 4);

            if (dy < -160 || dy > 160)
                Fail(index, "height correction out of range");
            ++changed;
            if (!Rando::SpriteName(id))
                Fail(index, "unknown target kind");
            // swaps have to make sense: swimmers stay in the water and nothing else gets in,
            // and what never moves only replaces something that never moved either
            if (IsSwimmer(id) != IsSwimmer(R16(a)))
                Fail(index, "a swap crossed the water line");
            if (IsFixed(id) && !IsFixed(R16(a)))
                Fail(index, "something that never moves replaced a moving enemy");
        }
        else if ((id == 52 || id == 206) && R16(b + 4) == 0xF000 && std::memcmp(a, b, 4) == 0
                 && std::memcmp(a + 6, b + 6, 18) == 0)
            ++flags;    // a checkpoint flag or a Gold Ring moved out of reach
        else if (id == 97 && (a[11] >> 4) && b[11] == (a[11] & 0x0F) && std::memcmp(a, b, 11) == 0
                 && std::memcmp(a + 12, b + 12, 12) == 0)
            ;           // a secret goal turned into an ordinary one
        else if (std::memcmp(a, b, 24) != 0)
            Fail(index, "untouched sprite was modified");
        for (uint32_t i = 0; i < uSize; i += 4)
            listed = listed || R16(out + uOff + i) == id;
        if (!listed)
            Fail(index, "kind is missing from the load list");
        ++count;
    }
    // the spawners of the item rain follow the sprites of the level
    for (uint32_t i = 0; i < res.rain; ++i)
    {
        const uint8_t *spr = out + nOff + (count + i) * 24;
        bool listed = false, known = false;
        static const uint8_t items[] = { 1, 2, 4, 5, 6, 7, 9, 11 };

        if (R16(spr) != 252)
            Fail(index, "item rain added something that is no item spawner");
        for (uint8_t item : items)
            known = known || (spr[11] >> 4) == item;
        if (!known || (spr[10] >> 4) != 1 || (spr[9] & 0x0F) == 0)
            Fail(index, "item spawner has odd settings");
        for (uint32_t k = 0; k < uSize; k += 4)
            listed = listed || R16(out + uOff + k) == 252;
        if (!listed)
            Fail(index, "item spawner is missing from the load list");
    }
    if (R16(out + nOff + (count + res.rain) * 24) != 0xFFFF && (count + res.rain + 1) * 24 <= nSize)
        Fail(index, "sprite terminator is missing");
    if (changed != res.swapped || count != res.total || flags != (uint32_t)res.checkpoints + res.rings)
        Fail(index, "counters in the result are off");
}

// Levels trade places within their kind only: ordinary levels, levels with a secret exit, the
// castles of the Koopalings, the towers. With a fixed seed that is one permutation per kind. With a
// new roll on every visit a place never gets its own level nor the one handed out before, and with
// a list of finished levels none of those either, until the kind has run out and starts over.
static void TestLevels()
{
    const struct { const uint16_t *list; uint32_t count; } kinds[] =
    {
        { Rando::kLevelsNormal, Rando::kLevelsNormalCount },
        { Rando::kLevelsSecret, Rando::kLevelsSecretCount },
        { Rando::kLevelsCastle, Rando::kLevelsCastleCount },
        { Rando::kLevelsTower, Rando::kLevelsTowerCount },
        { Rando::kLevelsTowerSecret, Rando::kLevelsTowerSecretCount },
    };
    const int kindCount = 5;
    auto kindOf = [&](uint16_t code)
    {
        for (int k = 0; k < kindCount; ++k)
            for (uint32_t i = 0; i < kinds[k].count; ++i)
                if (kinds[k].list[i] == code)
                    return k;
        return -1;
    };
    uint32_t moved = 0, total = 0, rolls = 0, resets = 0;

    for (uint32_t seed = 1; seed <= 50; ++seed)
    {
        Rando::Settings s;
        uint32_t w = 0, n = 0;

        Rando::DefaultSettings(s);
        s.seed = seed;
        Rando::SetProgress(nullptr);

        // fixed seed
        s.rerollEachTry = 0;
        for (int k = 0; k < kindCount; ++k)
        {
            std::map<uint16_t, int> seen;

            for (uint32_t i = 0; i < kinds[k].count; ++i)
            {
                uint16_t node = kinds[k].list[i];

                if (!Rando::ShuffleLevel(s, 1, node >> 8, node & 0xFF, 0, w, n))
                {
                    Fail(i, "a level was not shuffled");
                    continue;
                }
                uint16_t out = (uint16_t)((w << 8) | n);

                if (kindOf(out) != k)
                    Fail(i, "fixed shuffle left its kind");
                if (seen[out]++)
                    Fail(i, "fixed shuffle is not a permutation");
                if (Rando::DropSecretExit())
                    Fail(i, "a secret exit was to be dropped");
                ++total;
                moved += out != node;
            }
        }

        // a new roll on every visit, nothing remembered
        s.rerollEachTry = 1;
        uint16_t last = 0;

        for (uint32_t visit = 0; visit < 400; ++visit)
        {
            int k = (int)(visit % kindCount);
            uint16_t node = kinds[k].list[(visit * 7 + seed) % kinds[k].count];

            if (!Rando::ShuffleLevel(s, seed * 1000 + visit, node >> 8, node & 0xFF, last, w, n))
            {
                Fail(visit, "a level was not rolled");
                continue;
            }
            uint16_t out = (uint16_t)((w << 8) | n);

            if (kindOf(out) != k)
                Fail(visit, "a roll left its kind");
            if (out == last)
                Fail(visit, "the same level twice in a row");
            if (out == node)
                Fail(visit, "a place got its own level");
            if (Rando::DropSecretExit() || Rando::ProgressReset())
                Fail(visit, "secret exit or progress handling is off");
            last = out;
            ++rolls;
        }

        // with a list of finished levels: every level played is finished right away, as in a
        // run without a single death. Nothing comes up twice before its kind has run out.
        Rando::Progress progress;

        std::memset(&progress, 0, sizeof(progress));
        Rando::SetProgress(&progress);
        last = 0;
        for (int k = 0; k < kindCount; ++k)
        {
            std::set<uint16_t> seenSince;

            for (uint32_t visit = 0; visit < kinds[k].count * 3; ++visit)
            {
                uint16_t node = kinds[k].list[(visit * 5 + seed) % kinds[k].count];

                if (!Rando::ShuffleLevel(s, seed * 5000 + k * 500 + visit, node >> 8, node & 0xFF, last, w, n))
                {
                    Fail(visit, "a level was not rolled");
                    continue;
                }
                uint16_t out = (uint16_t)((w << 8) | n);

                if (Rando::ProgressReset())
                {
                    ++resets;
                    // a kind only starts over when at most the place's own level and the last one were left
                    if (seenSince.size() + 2 < kinds[k].count)
                        Fail(visit, "a kind started over although levels were left");
                    seenSince.clear();
                }
                if (kindOf(out) != k || out == node || out == last)
                    Fail(visit, "a roll with finished levels is off");
                if (seenSince.count(out))
                    Fail(visit, "a finished level came up again");
                seenSince.insert(out);
                Rando::MarkDone(progress, out);
                if (!Rando::IsDone(progress, out) || Rando::LevelSlot(out) < 0)
                    Fail(visit, "a level could not be marked as finished");
                last = out;
            }
        }
        Rando::SetProgress(nullptr);

        // what never moves
        if (Rando::ShuffleLevel(s, 1, 6, 16, 0, w, n) || Rando::ShuffleLevel(s, 1, 9, 16, 0, w, n)
            || Rando::ShuffleLevel(s, 1, 1, 20, 0, w, n))
            Fail(0, "castle of Bowser or toad house was shuffled");
        s.castles = 0;
        if (Rando::ShuffleLevel(s, 1, 1, 16, 0, w, n) || Rando::ShuffleLevel(s, 1, 1, 14, 0, w, n))
            Fail(0, "a castle or tower was shuffled although that is off");
    }
    std::printf("levels: fixed seed moves %u of %u, %u rolls checked, kinds started over %u times\n", moved, total, rolls,
                resets);
}

struct Course
{
    uint8_t world, number, area;
    std::vector<uint8_t> data;
};

static const Rando::KindCost *CostOf(uint16_t id)
{
    for (uint32_t i = 0; i < Rando::kKindCostCount; ++i)
        if (Rando::kKindCosts[i].id == id)
            return &Rando::kKindCosts[i];
    return nullptr;
}

static const Rando::LevelRoom *RoomOf(uint32_t key)
{
    for (uint32_t i = 0; i < Rando::kLevelRoomCount; ++i)
        if (Rando::kLevelRooms[i].key == key)
            return &Rando::kLevelRooms[i];
    return nullptr;
}

static std::set<uint16_t> LoadList(const uint8_t *course)
{
    std::set<uint16_t> ids;
    uint32_t off = R32(course + 8 * 8), size = R32(course + 8 * 8 + 4);

    for (uint32_t i = 0; i + 4 <= size; i += 4)
        ids.insert(R16(course + off + i));
    return ids;
}

int main(int argc, char **argv)
{
    FILE *f = std::fopen(argv[1], "rb");
    std::vector<Course> courses;
    uint8_t head[8];

    while (std::fread(head, 8, 1, f) == 1)
    {
        Course c;
        uint32_t size = R32(head + 4);

        c.world = head[0];
        c.number = head[1];
        c.area = head[2];
        c.data.resize(size);
        if (std::fread(c.data.data(), 1, size, f) != size)
            return 1;
        courses.push_back(c);
    }
    std::fclose(f);

    // the courses of a level follow each other in the file
    std::vector<std::vector<size_t>> levels;

    for (size_t i = 0; i < courses.size(); ++i)
    {
        if (i == 0 || courses[i].world != courses[i - 1].world || courses[i].number != courses[i - 1].number)
            levels.push_back(std::vector<size_t>());
        levels.back().push_back(i);
    }

    static uint8_t out[Rando::MAX_AREAS][0x4000], again[0x4000];

    for (int mode = 0; mode < 3; ++mode)
        for (int perInstance = 0; perInstance < 2; ++perInstance)
            for (int pooled = 0; pooled < 2; ++pooled)
            {
                Rando::Settings s;
                std::map<uint16_t, uint32_t> targets;
                uint32_t swapped = 0, total = 0, touched = 0, maxTypes = 0, maxGrow = 0, maxSize = 0;
                uint32_t planned = 0, known = 0, freshKinds = 0, freshLevels = 0, enemies = 0, rainSpawners = 0;

                Rando::DefaultSettings(s);
                s.mode = (uint8_t)mode;
                s.perInstance = (uint8_t)perInstance;
                // the item rain is tried in half of the passes
                s.itemRain = (uint8_t)(mode != 1);

                for (uint32_t seed = 1; seed <= 5; ++seed)
                    for (size_t l = 0; l < levels.size(); ++l)
                    {
                        const uint8_t *ptrs[Rando::MAX_AREAS] = { nullptr, nullptr, nullptr, nullptr };
                        uint32_t sizes[Rando::MAX_AREAS] = { 0, 0, 0, 0 };
                        uint32_t levelSeed = seed * 7919 + (uint32_t)l;
                        std::set<uint16_t> before, after;

                        for (size_t i : levels[l])
                        {
                            const Course &c = courses[i];

                            if (c.area < 1 || c.area > Rando::MAX_AREAS)
                                continue;
                            ptrs[c.area - 1] = c.data.data();
                            sizes[c.area - 1] = (uint32_t)c.data.size();
                            for (uint16_t id : LoadList(c.data.data()))
                                before.insert(id);
                        }
                        // checkpoint flags go on every other pass, as they do where levels are shuffled
                        Rando::SetNodeShuffled((seed & 1) != 0);
                        Rando::PlanLevel(ptrs, sizes, s, levelSeed, pooled != 0);

                        Rando::Plan plan = Rando::LastPlan();

                        ++planned;
                        known += plan.known;
                        after = before;
                        for (size_t i : levels[l])
                        {
                            const Course &c = courses[i];
                            Rando::Result res, res2;
                            uint32_t a = c.area - 1;

                            if (Rando::CourseSize(c.data.data()) != c.data.size())
                                Fail(i, "CourseSize does not match the file size");
                            total += 1;
                            if (!Rando::RandomizeCourse(c.data.data(), (uint32_t)c.data.size(), out[a], sizeof(out[a]),
                                                        s, levelSeed, a, res))
                                continue;
                            Rando::RandomizeCourse(c.data.data(), (uint32_t)c.data.size(), again, sizeof(again), s,
                                                   levelSeed, a, res2);
                            if (res.outSize != res2.outSize || std::memcmp(out[a], again, res.outSize) != 0)
                                Fail(i, "not reproducible");
                            Check(i, c.data, out[a], res);
                            ++touched;
                            swapped += res.swapped;
                            enemies += res.total;
                            if (res.typesAfter > maxTypes) maxTypes = res.typesAfter;
                            if (res.typesAfter > res.typesBefore && (uint32_t)(res.typesAfter - res.typesBefore) > maxGrow)
                                maxGrow = res.typesAfter - res.typesBefore;
                            if (res.outSize > maxSize) maxSize = res.outSize;
                            rainSpawners += res.rain;
                            if (res.rain && !plan.rain)
                                Fail(i, "item rain in a level whose plan has none");
                            if (res.rain)
                                after.insert(252);
                            for (int m = 0; m < res.mapCount; ++m)
                                targets[res.mapTo[m]] += res.mapHits[m];
                            for (uint16_t id : LoadList(out[a]))
                                after.insert(id);
                        }

                        // What the level loads on top of before has to be what the plan announced, and
                        // the plan has to leave the reserve of every memory pool alone.
                        uint32_t spent[Rando::MEM_COUNT] = { 0, 0, 0, 0, 0, 0 };
                        uint32_t added = 0;

                        for (uint16_t id : after)
                        {
                            if (before.count(id))
                                continue;
                            ++added;

                            bool announced = id == 252 && plan.rain;

                            for (uint32_t k = 0; k < plan.freshCount; ++k)
                                announced = announced || plan.fresh[k] == id;
                            if (!announced)
                                Fail(levels[l][0], "a new kind is loaded that the plan does not know about");

                            const Rando::KindCost *cost = CostOf(id);

                            if (!cost)
                                Fail(levels[l][0], "a new kind without a measured cost is loaded");
                            else
                                for (uint32_t r = 0; r < Rando::MEM_COUNT; ++r)
                                    spent[r] += cost->cost[r];
                        }
                        if (added)
                        {
                            const Rando::LevelRoom *room = RoomOf(plan.key);
                            static const uint32_t reserve[Rando::MEM_COUNT] = { 640, 256, 4, 128, 64, 64 };

                            ++freshLevels;
                            freshKinds += added;
                            if (!room)
                                Fail(levels[l][0], "new kinds in a level that was never measured");
                            else
                            {
                                for (uint32_t r = 0; r < Rando::MEM_VRAM_A; ++r)
                                    if (spent[r] + reserve[r] > room->free[r])
                                        Fail(levels[l][0], "the plan eats into the reserve of a memory pool");
                                if (pooled)
                                {
                                    if (spent[Rando::MEM_VRAM_A] + spent[Rando::MEM_VRAM_B] + 128 > room->both)
                                        Fail(levels[l][0], "the plan eats into the VRAM reserve");
                                }
                                else if (spent[Rando::MEM_VRAM_A] + 64 > room->free[Rando::MEM_VRAM_A]
                                         || spent[Rando::MEM_VRAM_B] + 64 > room->free[Rando::MEM_VRAM_B])
                                    Fail(levels[l][0], "the plan eats into the reserve of a VRAM bank");
                            }
                        }
                    }
                std::printf("mode %d per-instance %d pooled %d: %u of %u courses, %u of %u sprites swapped, max kinds %u, "
                            "max growth %u, max size %u, target kinds %zu, %u of %u levels known, "
                            "%u got new kinds (%.1f each), %u item spawners\n", mode, perInstance, pooled, touched, total,
                            swapped, enemies, maxTypes, maxGrow, maxSize, targets.size(), known, planned, freshLevels,
                            freshLevels ? (double)freshKinds / freshLevels : 0.0, rainSpawners);
            }

    // without a plan an area only draws on its own load list
    {
        Rando::Settings s;
        const uint8_t *none[Rando::MAX_AREAS] = { nullptr, nullptr, nullptr, nullptr };
        uint32_t zero[Rando::MAX_AREAS] = { 0, 0, 0, 0 };
        uint32_t grown = 0;

        Rando::DefaultSettings(s);
        Rando::PlanLevel(none, zero, s, 1, true);
        for (size_t i = 0; i < courses.size(); ++i)
        {
            Rando::Result res;

            if (Rando::RandomizeCourse(courses[i].data.data(), (uint32_t)courses[i].data.size(), out[0], sizeof(out[0]),
                                       s, 77 + (uint32_t)i, courses[i].area - 1, res))
                grown += res.typesAfter != res.typesBefore;
        }
        if (grown)
            Fail(0, "an area without a plan loaded a new kind");
    }

    TestLevels();
    std::printf("%d errors\n", g_errors);
    return g_errors ? 1 : 0;
}
