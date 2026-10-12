// Runs the randomizer core on a PC against every course file of the game.
// Input: a file of blocks (u32 size, data) as written by tools/make_blob.py.
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "LevelTable.hpp"
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

        if (b == 8)
            continue;
        if (R32(out + b * 8) != off || R32(out + b * 8 + 4) != size)
            Fail(index, "header of an unrelated block changed");
        if (b != 7 && std::memcmp(&in[off], out + off, size) != 0)
            Fail(index, "contents of an unrelated block changed");
    }

    uint32_t sOff = R32(&in[7 * 8]), sSize = R32(&in[7 * 8 + 4]);
    uint32_t uOff = R32(out + 8 * 8), uSize = R32(out + 8 * 8 + 4);
    uint32_t count = 0, changed = 0, flags = 0;

    if (uOff + uSize != res.outSize)
        Fail(index, "load list is not at the end");
    for (uint32_t i = 4; i < uSize; i += 4)
        if (R16(out + uOff + i) <= R16(out + uOff + i - 4))
            Fail(index, "load list is not ascending");

    while ((count + 1) * 24 <= sSize && R16(&in[sOff + count * 24]) != 0xFFFF)
    {
        const uint8_t *a = &in[sOff + count * 24];
        const uint8_t *b = out + sOff + count * 24;
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
        else if (std::memcmp(a, b, 24) != 0)
            Fail(index, "untouched sprite was modified");
        for (uint32_t i = 0; i < uSize; i += 4)
            listed = listed || R16(out + uOff + i) == id;
        if (!listed)
            Fail(index, "kind is missing from the load list");
        ++count;
    }
    if (R16(out + sOff + count * 24) != 0xFFFF && (count + 1) * 24 <= sSize)
        Fail(index, "sprite terminator is missing");
    if (changed != res.swapped || count != res.total || flags != (uint32_t)res.checkpoints + res.rings)
        Fail(index, "counters in the result are off");
}

// With a fixed seed the shuffle is a permutation inside each class. With a new roll on every
// visit any level may come up, never the one handed out before, and a level with a secret exit
// on an ordinary place loses that exit. Towers, castles and toad houses are always left alone.
static void TestLevels()
{
    const struct { const uint16_t *list; uint32_t count; } classes[] =
    {
        { Rando::kLevelsNormal, Rando::kLevelsNormalCount },
        { Rando::kLevelsSecret, Rando::kLevelsSecretCount },
    };
    auto isSecret = [](uint16_t code)
    {
        for (uint32_t k = 0; k < Rando::kLevelsSecretCount; ++k)
            if (Rando::kLevelsSecret[k] == code)
                return true;
        return false;
    };
    auto isKnown = [&](uint16_t code)
    {
        for (uint32_t k = 0; k < Rando::kLevelsNormalCount; ++k)
            if (Rando::kLevelsNormal[k] == code)
                return true;
        return isSecret(code);
    };
    uint32_t moved = 0, total = 0, secretOnSecret = 0, secretVisits = 0;
    std::map<uint16_t, int> spread;

    for (uint32_t seed = 1; seed <= 50; ++seed)
    {
        Rando::Settings s;
        uint32_t w = 0, n = 0;

        Rando::DefaultSettings(s);
        s.seed = seed;

        s.rerollEachTry = 0;
        for (const auto &c : classes)
        {
            std::map<uint16_t, int> seen;

            for (uint32_t i = 0; i < c.count; ++i)
            {
                if (!Rando::ShuffleLevel(s, 1, c.list[i] >> 8, c.list[i] & 0xFF, 0, w, n))
                {
                    Fail(i, "level of a class was not shuffled");
                    continue;
                }
                uint16_t out = (uint16_t)((w << 8) | n);

                if (isSecret(out) != isSecret(c.list[i]))
                    Fail(i, "fixed shuffle left its class");
                if (seen[out]++)
                    Fail(i, "fixed shuffle is not a permutation");
                if (Rando::DropSecretExit())
                    Fail(i, "fixed shuffle asked to drop a secret exit");
                ++total;
                moved += out != c.list[i];
            }
        }

        s.rerollEachTry = 1;
        uint16_t last = 0;

        for (uint32_t visit = 0; visit < 400; ++visit)
        {
            const auto &c = classes[visit % 2];
            uint16_t node = c.list[(visit * 7 + seed) % c.count];

            if (!Rando::ShuffleLevel(s, seed * 1000 + visit, node >> 8, node & 0xFF, last, w, n))
            {
                Fail(visit, "level of a class was not rolled");
                continue;
            }
            uint16_t out = (uint16_t)((w << 8) | n);

            if (!isKnown(out))
                Fail(visit, "rolled a level outside the pool");
            if (out == last)
                Fail(visit, "the same level twice in a row");
            if (Rando::DropSecretExit() != (isSecret(out) && !isSecret(node)))
                Fail(visit, "secret exit handling is off");
            if (isSecret(node))
            {
                ++secretVisits;
                secretOnSecret += isSecret(out);
            }
            ++spread[out];
            last = out;
        }
        if (Rando::ShuffleLevel(s, 1, 1, 14, 0, w, n) || Rando::ShuffleLevel(s, 1, 1, 16, 0, w, n)
            || Rando::ShuffleLevel(s, 1, 1, 20, 0, w, n))
            Fail(0, "tower, castle or toad house was shuffled");
    }
    std::printf("levels: fixed seed moves %u of %u, rolling reached %zu different levels, "
                "secret places got a secret level in %u of %u visits\n", moved, total, spread.size(),
                secretOnSecret, secretVisits);
}

// Item blocks trade contents within their block type: the same items before and after,
// Mini Mushrooms, coins and friends never move, and nothing but the object number changes.
static void TestItems()
{
    static const uint16_t rows[3][6] = { { 6, 11, 7, 8, 12, 0 }, { 20, 21, 24, 26, 22, 25 }, { 32, 33, 37, 38, 34, 0 } };
    static const uint16_t fixed[] = { 5, 9, 10, 23, 35, 16, 17, 18, 19, 27, 28, 29, 30, 31, 36, 39, 40, 100 };
    uint32_t changed = 0, total = 0;

    for (uint32_t seed = 1; seed <= 40; ++seed)
    {
        std::vector<uint8_t> data;
        std::vector<int> rowOf;
        Rando::Settings s;

        Rando::DefaultSettings(s);
        auto add = [&](uint16_t id, int row)
        {
            uint8_t rec[16] = { 0 };
            uint16_t x = (uint16_t)(rowOf.size() * 3), y = (uint16_t)(rowOf.size() * 7 % 50);

            rec[0] = (uint8_t)id; rec[1] = (uint8_t)(id >> 8);
            rec[2] = (uint8_t)x; rec[3] = (uint8_t)(x >> 8);
            rec[4] = (uint8_t)y; rec[5] = (uint8_t)(y >> 8);
            data.insert(data.end(), rec, rec + 16);
            rowOf.push_back(row);
        };
        // a different mix for every seed, some rows left with a single kind or none
        for (int r = 0; r < 3; ++r)
            for (int i = 0; i < 6; ++i)
                if (rows[r][i])
                    for (uint32_t c = 0; c < (seed * (r + 2) + i * 5) % 4; ++c)
                        add(rows[r][i], r);
        for (uint16_t id : fixed)
            add(id, -1);
        data.push_back(0xFF);
        data.push_back(0xFF);

        std::vector<uint8_t> before = data;
        std::map<uint16_t, int> balance;

        changed += Rando::RandomizeItems(data.data(), (uint32_t)data.size(), s, seed);
        for (size_t i = 0; i < rowOf.size(); ++i)
        {
            uint16_t was = R16(&before[i * 16]), is = R16(&data[i * 16]);

            ++total;
            if (std::memcmp(&data[i * 16 + 2], &before[i * 16 + 2], 14) != 0)
                Fail(i, "item shuffle touched more than the object number");
            if (rowOf[i] < 0)
            {
                if (is != was)
                    Fail(i, "a block that is not part of the shuffle was changed");
                continue;
            }
            bool sameRow = false;

            for (int k = 0; k < 6; ++k)
                sameRow = sameRow || (rows[rowOf[i]][k] && rows[rowOf[i]][k] == is);
            if (!sameRow)
                Fail(i, "item left its block type");
            ++balance[was];
            --balance[is];
        }
        for (const auto &entry : balance)
            if (entry.second != 0)
                Fail(seed, "the level has other items than before");
    }
    std::printf("items: %u of %u test blocks changed\n", changed, total);
}

int main(int argc, char **argv)
{
    FILE *f = std::fopen(argv[1], "rb");
    std::vector<std::vector<uint8_t>> courses;
    uint32_t size;

    while (std::fread(&size, 4, 1, f) == 1)
    {
        std::vector<uint8_t> data(size);

        if (std::fread(data.data(), 1, size, f) != size)
            return 1;
        courses.push_back(data);
    }
    std::fclose(f);

    static uint8_t out[0x4000], again[0x4000];

    for (int mode = 0; mode < 3; ++mode)
        for (int perInstance = 0; perInstance < 2; ++perInstance)
            for (int bosses = 0; bosses < 1; ++bosses)
            {
                Rando::Settings s;
                std::map<uint16_t, uint32_t> targets;
                uint32_t swapped = 0, total = 0, touched = 0, maxTypes = 0, maxGrow = 0, maxSize = 0;

                Rando::DefaultSettings(s);
                s.mode = (uint8_t)mode;
                s.perInstance = (uint8_t)perInstance;

                for (uint32_t seed = 1; seed <= 5; ++seed)
                    for (size_t i = 0; i < courses.size(); ++i)
                    {
                        const std::vector<uint8_t> &c = courses[i];
                        Rando::Result res, res2;

                        if (Rando::CourseSize(c.data()) != c.size())
                            Fail(i, "CourseSize does not match the file size");
                        total += 1;
                        if (!Rando::RandomizeCourse(c.data(), c.size(), out, sizeof(out), s, seed * 7919 + i, 0, res))
                            continue;
                        Rando::RandomizeCourse(c.data(), c.size(), again, sizeof(again), s, seed * 7919 + i, 0, res2);
                        if (res.outSize != res2.outSize || std::memcmp(out, again, res.outSize) != 0)
                            Fail(i, "not reproducible");
                        Check(i, c, out, res);
                        ++touched;
                        swapped += res.swapped;
                        if (res.typesAfter > maxTypes) maxTypes = res.typesAfter;
                        if (res.typesAfter > res.typesBefore && (uint32_t)(res.typesAfter - res.typesBefore) > maxGrow)
                            maxGrow = res.typesAfter - res.typesBefore;
                        if (res.outSize > maxSize) maxSize = res.outSize;
                        for (int m = 0; m < res.mapCount; ++m)
                            targets[res.mapTo[m]] += res.mapHits[m];
                    }
                std::printf("mode %d per-instance %d bosses %d: %u of %u courses, %u enemies, max kinds %u, max growth %u, "
                            "max size %u, target kinds %zu\n", mode, perInstance, bosses, touched, total, swapped,
                            maxTypes, maxGrow, maxSize, targets.size());
            }
    TestLevels();
    TestItems();
    std::printf("%d errors\n", g_errors);
    return g_errors ? 1 : 0;
}
