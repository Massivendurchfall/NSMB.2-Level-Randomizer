#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Randomizer.hpp"
#include "TestMode.hpp"

namespace CTRPluginFramework
{
    namespace
    {
        // The three places the plugin taps into. Addresses are for EUR 1.0, other builds
        // are located through the instruction patterns (a zero word matches anything).
        const u32   kAnyWord = 0;

        // void LoadArea(AreaData *area, const u8 *courseBin)
        // Keeps nothing but pointers into the course file, so a modified copy can be passed in.
        const u32   kLoaderEur = 0x003D39EC;
        const u32   kLoaderPattern[] =
        {
            0xE92D47F0, 0xE3A05000, 0xE1A04000, 0xE1A06001, kAnyWord,   0xE1A08005, 0xE3560000,
            0x0A000014, 0xE0861185, 0xE284708C, 0xE5910000, 0xE0962000, 0xE3550011, 0xE2840004
        };

        // void MakeLevelName(String *out, u32 type, u32 worldIndex, u32 levelIndex)
        // Builds "W-N" for the level archive, type 0 is the regular game.
        const u32   kNameEur = 0x001CEE60;
        const u32   kNamePattern[] =
        {
            0xE52DE004, 0xE3510000, 0xE24DD00C, 0x0A000009, 0xE3510002, 0x0A000012, 0xE3520019, 0xCA00000E
        };

        // void *FindLevelFile(ArchiveMgr *mgr, const String *name, u32 *outSize)
        // Looks a file up inside the level archive: the course files and the tile layers.
        const u32   kLookupEur = 0x003DDA78;
        const u32   kLookupPattern[] =
        {
            0xE92D4070, 0xE1A05001, kAnyWord,   0xE24DD008, 0xE1A04002,
            0xE3A06000, 0xE5911000, 0xE5D11010, 0xE3510001, 0x1A000004
        };

        // void SetTime(Timer *timer, u32 seconds), the timer keeps seconds << 12 at +0x18.
        // Only hooked to learn where the timer lives.
        const u32   kSetTimeEur = 0x00224548;
        const u32   kSetTimePattern[] =
        {
            0xE1A02601, 0xE5802018, 0xE5D0301C, 0xE1A01601, 0xE2811C0F, 0xE3530000
        };
        const u32   kTimerValueOffset = 0x18;

        const u32   kAreaCount = 4;
        const u32   kWorldCount = 9;        // file names use world numbers 1 to 9
        const u32   kCourseCap = 0x4000;    // the largest course file in the game is 9900 bytes
        const u32   kConfigMagic = 0x32444E52;
        const u32   kConfigVersion = 9;
        const char  *kConfigFile = "nsmb2rando.cfg";
        const char  *kLogFile = "nsmb2rando.log";

        struct ConfigFile
        {
            u32             magic;
            u32             version;
            Rando::Settings settings;
        };

        Rando::Settings g_set;
        Hook            g_loaderHook, g_nameHook, g_lookupHook, g_timeHook;
        u32             g_timeFn = 0;
        u32             *g_timer = nullptr; // the game's level timer, known once a level has started
        u32             g_loader = 0;
        u32             g_nameFn = 0;
        u32             g_lookupFn = 0;

        // The game reads course data a word at a time. A real console faults on an unaligned
        // address (the emulator does not), so the copies must start on a word boundary.
        alignas(8) u8   g_course[kAreaCount][kCourseCap];
        Rando::Result   g_result[kAreaCount];
        bool            g_resultValid[kAreaCount];
        u32             g_levelKey = 0;
        u32             g_tries = 0;
        u32             g_sessionSalt = 0;  // differs from one start of the game to the next
        bool            g_loadedSinceName = true;
        u16             g_askedLevel = 0;   // (world << 8) | number the game asked for
        u16             g_givenLevel = 0;   // what it got instead
        volatile u32    g_itemsChanged = 0;
        volatile u32    g_tileLayers = 0;   // tile layers seen since the last report
        volatile u32    g_loads = 0;        // levels loaded so far, bumped by the game thread
        volatile u32    g_activity = 0;     // bumped by every hook that did something
        u32             g_activitySeen = 0;
        u32             g_quietTicks = 0;
        bool            g_reportDue = false;
        u32             g_lastItems = 0;

        MenuEntry       *g_eEnabled, *g_eEnemies, *g_eLevels, *g_eItems, *g_eMode, *g_eReroll, *g_eSeed;

        u32     FindCode(const u32 *pattern, u32 words, u32 hint)
        {
            const u32   textStart = 0x00100000;
            const u32   textEnd = textStart + Process::GetTextSize();

            for (u32 pass = 0; pass < 2; ++pass)
            {
                u32 addr = pass ? textStart : hint;
                u32 last = pass ? textEnd - words * 4 : hint;

                for (; addr <= last && addr + words * 4 <= textEnd; addr += 4)
                {
                    const u32   *code = reinterpret_cast<const u32 *>(addr);
                    u32         i = 0;

                    while (i < words && (pattern[i] == kAnyWord || code[i] == pattern[i]))
                        ++i;
                    if (i == words)
                        return addr;
                }
            }
            return 0;
        }

        // With a new roll on every visit, the count of visits alone would repeat the same
        // sequence after every start of the game. The moment of the first level load does not.
        u32     RollNumber(void)
        {
            if (!g_sessionSalt)
                g_sessionSalt = static_cast<u32>(svcGetSystemTick()) | 1;
            return g_tries + g_sessionSalt;
        }

        u32     LevelSeed(u32 salt)
        {
            u32 seed = g_set.seed * 0x9E3779B1u ^ salt;

            if (g_set.rerollEachTry)
                seed += RollNumber() * 0x85EBCA6Bu;
            return seed;
        }

        void    LoadAreaHook(u32 *area, const u8 *course)
        {
            const u8    *use = course;
            u32         index = area ? area[0] : kAreaCount;

            if (course && index < kAreaCount)
            {
                u32 size = Rando::CourseSize(course);

                // Area 0 always comes first, so it marks a new level
                if (index == 0)
                {
                    g_levelKey = Rando::Crc32(course, size);
                    if (!g_nameFn)
                        ++g_tries;
                    g_loadedSinceName = true;
                    std::memset(g_resultValid, 0, sizeof(g_resultValid));
                    ++g_loads;
                }
                ++g_activity;

                if (g_set.enabled && (g_set.enemies || g_set.levels || Rando::DropSecretExit()) && size
                    && Rando::RandomizeCourse(course, size, g_course[index], kCourseCap, g_set,
                                              LevelSeed(g_levelKey), index, g_result[index]))
                {
                    use = g_course[index];
                    g_resultValid[index] = true;
                }
            }

            HookContext::GetCurrent().OriginalFunction<void>(area, use);
        }

        void    LevelNameHook(void *out, u32 type, u32 worldIndex, u32 levelIndex)
        {
            if (type == 0 && worldIndex < kWorldCount)
            {
                u32 world = worldIndex + 1;
                u32 number = levelIndex + 1;

                // The game may ask for the name more than once per visit, the try only
                // counts up after a level has actually been loaded in between
                if (g_loadedSinceName)
                {
                    g_loadedSinceName = false;
                    ++g_tries;
                }

                g_askedLevel = static_cast<u16>((world << 8) | number);
                if (g_set.enabled && g_set.levels
                    && Rando::ShuffleLevel(g_set, RollNumber(), world, number, g_givenLevel, world, number))
                {
                    worldIndex = world - 1;
                    levelIndex = number - 1;
                }
                if (Test::ForcedLevel())
                {
                    world = Test::ForcedLevel() >> 8;
                    number = Test::ForcedLevel() & 0xFF;
                    worldIndex = world - 1;
                    levelIndex = number - 1;
                }
                g_givenLevel = static_cast<u16>((world << 8) | number);
            }

            HookContext::GetCurrent().OriginalFunction<void>(out, type, worldIndex, levelIndex);
        }

        void    SetTimeHook(u32 *timer, u32 seconds)
        {
            g_timer = timer;
            HookContext::GetCurrent().OriginalFunction<void>(timer, seconds);
        }

        // "course/course1_bgdatL1.bin" and its siblings hold the tiles of an area
        bool    IsTileLayer(const char *name, u32 &area, u32 &layer)
        {
            static const char   prefix[] = "course/course";
            static const char   middle[] = "_bgdatL";
            const u32           prefixLen = sizeof(prefix) - 1;
            const u32           middleLen = sizeof(middle) - 1;

            if (std::strncmp(name, prefix, prefixLen) != 0 || name[prefixLen] < '1' || name[prefixLen] > '4')
                return false;
            if (std::strncmp(name + prefixLen + 1, middle, middleLen) != 0)
                return false;
            area = name[prefixLen] - '1';
            layer = name[prefixLen + 1 + middleLen] - '0';
            return layer < 3;
        }

        void    *FindFileHook(void *mgr, void **nameObj, u32 *outSize)
        {
            u32     size = 0;
            char    *digit = nullptr;

            // testing: "course/course1..." and "course/course2..." trade places
            if (Test::SwapAreas() && nameObj && nameObj[1])
            {
                char    *text = static_cast<char *>(nameObj[1]);

                if (std::strncmp(text, "course/course", 13) == 0 && (text[13] == '1' || text[13] == '2'))
                {
                    digit = text + 13;
                    *digit ^= '1' ^ '2';
                }
            }

            void    *data = HookContext::GetCurrent().OriginalFunction<void *>(mgr, nameObj,
                                                                               outSize ? outSize : &size);

            if (digit)
                *digit ^= '1' ^ '2';

            if (outSize)
                size = *outSize;
            if (Test::Active() && nameObj && reinterpret_cast<u32>(nameObj[1]) >= 0x00100000
                && Process::CheckAddress(reinterpret_cast<u32>(nameObj[1]), MEMPERM_READ))
                Test::Note(static_cast<const char *>(nameObj[1]));
            if (!data || !size || !nameObj || !g_set.enabled || !g_set.items)
                return data;

            // the name object is the game's string class: a vtable followed by the character pointer
            const char  *name = static_cast<const char *>(nameObj[1]);
            u32         area, layer;

            if (reinterpret_cast<u32>(name) < 0x00100000 || !Process::CheckAddress(reinterpret_cast<u32>(name), MEMPERM_READ))
                return data;
            if (IsTileLayer(name, area, layer))
            {
                g_itemsChanged += Rando::RandomizeItems(static_cast<u8 *>(data), size, g_set,
                                                        LevelSeed(g_givenLevel * 0x01000193u + area * 4 + layer));
                ++g_tileLayers;
                ++g_activity;
            }
            return data;
        }

        void    LoadConfig(void)
        {
            ConfigFile  cfg;
            File        file(kConfigFile, File::READ);

            Rando::DefaultSettings(g_set);
            if (file.IsOpen() && file.Read(&cfg, sizeof(cfg)) == 0 && cfg.magic == kConfigMagic
                && cfg.version == kConfigVersion)
                g_set = cfg.settings;
        }

        void    SaveConfig(void)
        {
            ConfigFile  cfg;

            std::memset(&cfg, 0, sizeof(cfg));
            cfg.magic = kConfigMagic;
            cfg.version = kConfigVersion;
            cfg.settings = g_set;

            File    file(kConfigFile, File::RWC | File::TRUNCATE);

            file.Write(&cfg, sizeof(cfg));
        }

        const char  *OnOff(u8 value)
        {
            return value ? "On" : "Off";
        }

        std::string KindName(u16 id)
        {
            const char  *name = Rando::SpriteName(id);

            return name ? std::string(name) : "Sprite " + std::to_string(id);
        }

        std::string LevelFile(u16 code)
        {
            return std::to_string(code >> 8) + "-" + std::to_string(code & 0xFF);
        }

        void    RefreshNames(void)
        {
            static const char *const modes[] = { "Safe", "Wild", "Chaos" };

            g_eEnabled->Name() = std::string("Randomizer: ") + OnOff(g_set.enabled);
            g_eEnemies->Name() = std::string("Random enemies: ") + OnOff(g_set.enemies);
            g_eLevels->Name() = std::string("Random levels: ") + OnOff(g_set.levels);
            g_eItems->Name() = std::string("Random items: ") + OnOff(g_set.items);
            g_eMode->Name() = std::string("Enemy mix: ") + modes[g_set.mode > 2 ? 0 : g_set.mode];
            g_eReroll->Name() = std::string("New roll after every death: ") + OnOff(g_set.rerollEachTry);
            g_eSeed->Name() = "Seed: " + std::to_string(g_set.seed);
        }

        void    Changed(void)
        {
            RefreshNames();
            SaveConfig();
        }

        // Every switch is tied to its field in g_set through SetArg
        void    ToggleEntry(MenuEntry *entry)
        {
            u8  *flag = static_cast<u8 *>(entry->GetArg());

            *flag = !*flag;
            Changed();
        }

        void    ChooseMode(MenuEntry *entry)
        {
            Keyboard    kb("Which enemies may replace which?",
                           { "Safe: walkers, flyers and swimmers stay apart",
                             "Wild: flyers also replace walkers",
                             "Chaos: Thwomps and Bullet Bills join in" });
            int         choice = kb.Open();

            if (choice >= 0)
            {
                g_set.mode = static_cast<u8>(choice);
                Changed();
            }
        }

        void    ChooseSeed(MenuEntry *entry)
        {
            Keyboard    kb("Enter a seed.\nThe same seed gives the same game.");
            u32         value = g_set.seed;

            kb.IsHexadecimal(false);
            if (kb.Open(value, g_set.seed) == 0)
            {
                g_set.seed = value;
                Changed();
            }
        }

        void    RollSeed(MenuEntry *entry)
        {
            g_set.seed = static_cast<u32>(svcGetSystemTick() % 1000000u);
            Changed();
            MessageBox("New seed: " + std::to_string(g_set.seed) + "\nApplies from the next level on.")();
        }

        // A shuffled game can strand you. Running the clock down is the one exit the game
        // always offers, so this leaves a second on it.
        bool    RunDownTimer(void)
        {
            u32 addr = reinterpret_cast<u32>(g_timer) + kTimerValueOffset;

            if (!g_timer || !Process::CheckAddress(addr, MEMPERM_READ | MEMPERM_WRITE))
                return false;
            *reinterpret_cast<u32 *>(addr) = 1 << 12;
            return true;
        }

        void    GiveUp(MenuEntry *entry)
        {
            if (!g_timer)
            {
                MessageBox("This only works after a level has been started.")();
                return;
            }
            if (!MessageBox("Give up this level?",
                            "The timer runs out as soon as you close this menu. Mario loses a life "
                            "and you are back on the map.\n\nOnly use this inside a level.",
                            DialogType::DialogYesNo)())
                return;
            RunDownTimer();
        }

        void    ShowLastLevel(MenuEntry *entry)
        {
            std::string text;
            u32         swapped = 0;

            if (!g_loader)
                text += "Enemy swaps are not supported on this version of the game.\n";
            if (!g_nameFn)
                text += "Level shuffle is not supported on this version of the game.\n";
            if (!g_lookupFn)
                text += "Item shuffle is not supported on this version of the game.\n";
            if (g_askedLevel && g_askedLevel != g_givenLevel)
                text += "Level file " + LevelFile(g_askedLevel) + " was replaced by " + LevelFile(g_givenLevel) + "\n";
            if (g_lastItems)
                text += std::to_string(g_lastItems) + " item blocks changed\n";
            for (u32 i = 0; i < kAreaCount; ++i)
            {
                if (!g_resultValid[i])
                    continue;

                const Rando::Result &res = g_result[i];

                swapped += res.swapped;
                if (res.mapCount)
                    text += "Area " + std::to_string(i + 1) + ":\n";
                for (u32 m = 0; m < res.mapCount; ++m)
                    text += "  " + std::to_string(res.mapHits[m]) + "x " + KindName(res.mapFrom[m])
                         + " became " + KindName(res.mapTo[m]) + "\n";
            }
            if (text.empty())
                text = "Nothing was changed in the last level.";
            MessageBox("Last level, " + std::to_string(swapped) + " enemies swapped", text)();
        }

        // The log is what a bug report needs: which level, which swaps. It starts over
        // once it has grown past a size that is still quick to read on the console.
        void    AppendLog(const std::string &text)
        {
            const u64   kMaxLog = 128 * 1024;
            bool        restart = false;

            {
                File    probe(kLogFile, File::READ);

                restart = probe.IsOpen() && probe.GetSize() > kMaxLog;
            }

            File    file(kLogFile, File::RWC | (restart ? File::TRUNCATE : File::APPEND));

            file.Write(text.c_str(), text.size());
        }

        void    ReportLevel(void)
        {
            u32         swapped = 0;
            std::string log;
            std::string note;
            char        line[96];

            g_lastItems = g_itemsChanged;
            g_itemsChanged = 0;

            std::snprintf(line, sizeof(line), "Level %08lX file %s -> %s try %lu seed %lu items %lu in %lu layers\n",
                          g_levelKey, LevelFile(g_askedLevel).c_str(), LevelFile(g_givenLevel).c_str(), g_tries,
                          g_set.seed, g_lastItems, g_tileLayers);
            g_tileLayers = 0;
            log = line;
            for (u32 i = 0; i < kAreaCount; ++i)
            {
                if (!g_resultValid[i])
                    continue;

                const Rando::Result &res = g_result[i];

                swapped += res.swapped;
                std::snprintf(line, sizeof(line), " Area %lu: %u of %u, kinds %u to %u\n", i + 1,
                              res.swapped, res.total, res.typesBefore, res.typesAfter);
                log += line;
                for (u32 m = 0; m < res.mapCount; ++m)
                    log += "  " + std::to_string(res.mapHits[m]) + "x " + KindName(res.mapFrom[m]) + " -> "
                        + KindName(res.mapTo[m]) + "\n";
            }
            AppendLog(log);

            if (g_askedLevel != g_givenLevel)
                note = "new level";
            if (swapped)
                note += std::string(note.empty() ? "" : ", ") + std::to_string(swapped) + " enemies";
            if (g_lastItems)
                note += std::string(note.empty() ? "" : ", ") + std::to_string(g_lastItems) + " items";
            if (g_set.notify && !note.empty())
                OSD::Notify("Randomizer: " + note);     // only the self test turns this on
        }

        // Runs on every pass of the plugin loop. Message and log come from here so the
        // game thread never touches files inside the hooks.
        void    OnTick(void)
        {
            Test::OnFrame(g_loads);

            // Areas and tile layers arrive one after the other while a level loads.
            // Once the hooks have been quiet for a moment the level is complete.
            if (g_activity != g_activitySeen)
            {
                g_activitySeen = g_activity;
                g_quietTicks = 0;
                g_reportDue = true;
            }
            else if (g_reportDue && ++g_quietTicks == 150)
            {
                g_reportDue = false;
                if (g_loads)
                    ReportLevel();
            }
        }

        MenuEntry   *Switch(u8 &flag, const std::string &note)
        {
            MenuEntry   *entry = new MenuEntry("", nullptr, ToggleEntry, note);

            entry->SetArg(&flag);
            return entry;
        }

        void    InitMenu(PluginMenu &menu)
        {
            const std::string   next = "\n\nApplies from the next level you enter.";

            g_eEnabled = Switch(g_set.enabled, "Master switch for everything below." + next);
            g_eEnemies = Switch(g_set.enemies,
                "Every enemy is rolled on its own, and only into something that fits the place. "
                "Gold Rings are gone while this is on: the game crashes when it has to turn "
                "a swapped enemy into gold." + next);
            g_eLevels = Switch(g_set.levels,
                "Entering a level loads a different one, any level can come up. Towers, castles, toad "
                "houses and cannons stay put, so the map keeps working. "
                "Checkpoint flags are gone while this is on, a checkpoint from another level would lead nowhere." + next);
            g_eItems = Switch(g_set.items,
                "The power-up blocks of a level trade their contents, so the Star may be where the "
                "Fire Flower was. Coins, beanstalks and Mini Mushrooms stay as they are." + next);
            g_eMode = new MenuEntry("", nullptr, ChooseMode,
                "Safe: walkers stay walkers, flyers stay flyers, swimmers stay swimmers.\n"
                "Wild: Boos and other flyers also replace walkers.\n"
                "Chaos: Thwomps, Bullet Bills and Banzai Bills join in on land. Can get unfair.\n"
                "Fish stay in the water in every mode." + next);
            g_eReroll = Switch(g_set.rerollEachTry,
                "On: enemies, level and items are rolled again every time you enter a level, so also after "
                "every death.\n"
                "Off: the game stays the same for as long as the seed stays the same.");
            g_eSeed = new MenuEntry("", nullptr, ChooseSeed,
                "The seed decides every roll. The same seed gives the same game again.");

            menu += g_eEnabled;
            menu += g_eEnemies;
            menu += g_eLevels;
            menu += g_eItems;
            menu += g_eMode;
            menu += g_eReroll;
            menu += g_eSeed;
            menu += new MenuEntry("Roll a random seed", nullptr, RollSeed);
            menu += new MenuEntry("Give up this level", nullptr, GiveUp,
                                  "Stuck? This runs the timer down, Mario loses a life and you return to the map.");
            menu += new MenuEntry("Show last level", nullptr, ShowLastLevel,
                                  "Lists what was changed in the level loaded last.");
            RefreshNames();
        }

        u32     Install(Hook &hook, const u32 *pattern, u32 words, u32 hint, u32 callback)
        {
            u32 addr = FindCode(pattern, words, hint);

            if (addr)
            {
                hook.InitializeForMitm(addr, callback);
                if (hook.Enable() != HookResult::Success)
                    addr = 0;
            }
            return addr;
        }
    }

    // Runs before the game starts, so the hooks are in place ahead of the first level
    void    PatchProcess(FwkSettings &settings)
    {
        Rando::DefaultSettings(g_set);
        g_loader = Install(g_loaderHook, kLoaderPattern, sizeof(kLoaderPattern) / 4, kLoaderEur,
                           reinterpret_cast<u32>(LoadAreaHook));
        g_nameFn = Install(g_nameHook, kNamePattern, sizeof(kNamePattern) / 4, kNameEur,
                           reinterpret_cast<u32>(LevelNameHook));
        g_lookupFn = Install(g_lookupHook, kLookupPattern, sizeof(kLookupPattern) / 4, kLookupEur,
                             reinterpret_cast<u32>(FindFileHook));
        g_timeFn = Install(g_timeHook, kSetTimePattern, sizeof(kSetTimePattern) / 4, kSetTimeEur,
                           reinterpret_cast<u32>(SetTimeHook));
    }

    void    OnProcessExit(void)
    {
        g_loaderHook.Disable();
        g_nameHook.Disable();
        g_lookupHook.Disable();
        g_timeHook.Disable();
    }

    int     main(void)
    {
        PluginMenu  *menu = new PluginMenu("NSMB2 Randomizer", 0, 4, 7,
            "Randomizes enemies, levels and items of New Super Mario Bros. 2 based on a seed.\n"
            "Every setting applies from the next level on.");

        menu->SynchronizeWithFrame(true);
        LoadConfig();
        Test::Init(g_set);
        Test::SetGiveUp(RunDownTimer);
        if (Test::Active())
        {
            char    line[64];

            std::snprintf(line, sizeof(line), "hooks %08lX %08lX %08lX %08lX", g_loader, g_nameFn, g_lookupFn,
                          g_timeFn);
            Test::Note(line);
        }
        InitMenu(*menu);
        *menu += OnTick;

        if (!g_loader && !g_nameFn && !g_lookupFn)
            OSD::Notify("Randomizer: unsupported game version, staying off", Color::Red);

        menu->Run();

        // Run only returns while the game is closing. The menu is deliberately not deleted:
        // tearing it down frees the entries a second time and crashes a real console on exit,
        // and the process is about to go away with all of its memory anyway.
        return (0);
    }
}
