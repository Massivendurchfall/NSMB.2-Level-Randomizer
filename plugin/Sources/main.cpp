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
        // The places the plugin taps into. Addresses are for EUR 1.0, other builds
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
        // Looks a file up inside the level archive. Hooked to learn how the game calls it, the
        // plugin then fetches the course files of all areas the same way.
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

        // void *VramAlloc(VramAllocator *self, u32 size, s32 alignment), 0 when the bank is full.
        // void VramFree(VramAllocator *self, void *memory)
        // The game keeps one allocator per VRAM bank and has no answer to a full one: it sends the
        // data to address 0 anyway and then waits for the graphics chip forever. That is the black
        // screen with the music still playing. The hooks let a full bank borrow from the other.
        const u32   kVramAllocEur = 0x0047BC90;
        const u32   kVramAllocPattern[] =
        {
            0xE92D47F0, 0xE3520004, 0xE1A04000, 0xB3A02004, 0xE2420001, 0xE1100002, 0xE2810003,
            0x13A02004, 0xE3C06003, 0xE284800C
        };
        const u32   kVramFreeEur = 0x0047BBFC;
        const u32   kVramFreePattern[] =
        {
            0xE3510000, 0x012FFF1E, 0xE92D41F0, 0xE1A04000, 0xE280500C, 0xE1A07001, 0xE1A00005,
            kAnyWord,   0xE5946040, 0xE3A08000
        };
        // where the 3DS maps the two banks
        const u32   kVramA = 0x1F000000;
        const u32   kVramB = 0x1F300000;
        const u32   kVramEnd = 0x1F600000;
        // An allocator keeps its free blocks in a list at +0x4C, a block has the next one at
        // +0x08 and its size at +0x0C
        const u32   kVramFreeList = 0x4C;

        // The game's table of heaps (EUR 1.0 only, checked by name before it is used): the
        // pointer to the ActorHeap comes first. A heap has its name at +0x14 and its free blocks
        // in a list at +0x74 (previous, next, count), a free block its size at +0x08.
        const u32   kHeapTableEur = 0x005EEC78;
        const u32   kMemoryDelay = 600;     // ticks, about ten seconds
        const u32   kMemoryStep = 96 * 1024;
        const u32   kMemoryWatch = 600 * 1024;  // below this much free actor heap every further drop is logged
        // The game's record of where Mario is (EUR 1.0 only): a pointer to it sits here, area and
        // entrance he came in through are at +0x13 and +0x14, the ones he is heading for at +0x1A
        // and +0x1B. Only read, to put doors and pipes into the log.
        const u32   kStageEur = 0x005E6288;
        // Mario's lives (EUR 1.0 only), a 32 bit number the game keeps between 0 and 1110. There
        // are two of them: the one that is saved, and the one a level counts down, which is
        // written back over the first when the level ends. Each sits in an object a fixed
        // address points to.
        const u32   kLivesHolders[][2] =
        {
            { 0x005E6130, 0x34 },   // the saved one
            { 0x005E61E8, 0x80 },   // the one in use during a level
        };
        const u32   kLivesLimit = 1110;

        // Saving (EUR 1.0 only). This is what the game does when Save is picked in its pause menu:
        //   CommitProgress(game)                 what was cleared since the last save becomes permanent
        //   CollectSave(saveMgr, slot, 0)        the state of the game goes into the buffer of the slot,
        //                                        0 is the ordinary save, 1 the quick save
        //   RequestSave(saveMgr)                 the save thread writes the buffer to the file
        // game and saveMgr are objects fixed addresses point to, the slot in use is a byte of game.
        const u32   kGameEur = 0x005EC298;
        const u32   kSaveMgrEur = 0x005EC33C;
        const u32   kSlotOffset = 0x1052;
        const u32   kSaveBusyOffset = 0x160;
        const u32   kCommitProgressEur = 0x001D70FC;
        const u32   kCollectSaveEur = 0x003F8630;
        const u32   kRequestSaveEur = 0x003FA474;
        // each function is checked against its first instructions before it is ever called
        const u32   kCommitProgressHead[] = { 0xE92D01F0, 0xE3A05000, 0xE1A02005, 0xE2804E27 };
        const u32   kCollectSaveHead[] = { 0xE92D4FF7, 0xE24DD018, 0xE1A05000, 0xE59D4020 };
        const u32   kRequestSaveHead[] = { 0xE92D4010, 0xE1A04000, 0xE3A00005, 0xE5C40158 };
        // The game only has an AreaHeap while a level is running, its place in the table of heaps
        // is zero on the world map. Mario counts as back on the map once it has been zero for this
        // many readings in a row, a good four seconds.
        const u32   kAreaHeapSlot = 5;
        const u32   kEmptyReadings = 8;

        const u32   kAreaCount = 4;
        const u32   kWorldCount = 9;        // file names use world numbers 1 to 9
        const u32   kCourseCap = 0x4000;    // the largest course file in the game is 9900 bytes
        const u32   kConfigMagic = 0x32444E52;
        const u32   kConfigVersion = 14;
        const char  *kConfigFile = "nsmb2rando.cfg";
        const char  *kLogFile = "nsmb2rando.log";
        const char  *kSeenFile = "nsmb2rando.seen";     // exists once the note below has been shown
        const u32   kWelcomeTick = 420;                 // frames into the first start, about seven seconds
        const char  *kMadeBy = "Made by L61r on Discord.\n\nFound a bug? Send me a message there, and attach "
                               "the file nsmb2rando.log from the plugin folder if you can.";

        struct ConfigFile
        {
            u32             magic;
            u32             version;
            Rando::Settings settings;
            Rando::Progress progress;
        };

        Rando::Settings g_set;
        Rando::Progress g_progress;             // the levels that were finished already
        volatile bool   g_configDue = false;    // the config has to be written, done outside the hooks
        Hook            g_loaderHook, g_nameHook, g_lookupHook, g_timeHook, g_vramAllocHook, g_vramFreeHook;
        u32             g_timeFn = 0;
        u32             g_vramAllocFn = 0;
        u32             g_vramFreeFn = 0;
        void            *g_vramBank[2];         // the game's allocator of each bank, learned from what it hands out
        volatile u32    g_vramMoved = 0;        // requests the other bank had to take
        volatile u32    g_vramFailed = 0;       // requests neither bank had room for
        u32             g_vramMovedSeen = 0;
        u32             g_ticks = 0;
        u32             g_memoryDue = 0;            // ticks until the memory of the level is read
        u32             g_loggedActors = 0;         // free bytes of the actor heap in the last log line
        u32             g_where = 0xFFFFFFFF;       // area and entrance bytes of the stage record, as last seen
        u64             g_startTick = 0;

        void            AppendLog(const std::string &text);
        int             SavedLives(void);
        void            Welcome(void);

        // seconds since the plugin started, every log entry carries it
        u32     Uptime(void)
        {
            return static_cast<u32>((svcGetSystemTick() - g_startTick) / SYSCLOCK_ARM11);
        }
        u32             g_lowActors = 0xFFFFFFFF;   // lowest free bytes seen since the last report
        u32             g_lowVram = 0xFFFFFFFF;
        void            *g_lookupMgr = nullptr; // the archive of the level files and the vtable of the
        u32             g_nameVtable = 0;       // game's string class, both learned from its own lookups
        u32             *g_timer = nullptr; // the game's level timer, known once a level has started
        u32             g_loader = 0;
        u32             g_nameFn = 0;
        u32             g_lookupFn = 0;

        // The game reads course data a word at a time. A real console faults on an unaligned
        // address (the emulator does not), so the copies must start on a word boundary.
        alignas(8) u8   g_course[kAreaCount][kCourseCap];
        Rando::Result   g_result[kAreaCount];
        bool            g_resultValid[kAreaCount];
        const u8        *g_original[kAreaCount];    // the course files as the game has them, for the log
        u32             g_levelKey = 0;
        u32             g_tries = 0;
        u32             g_sessionSalt = 0;  // differs from one start of the game to the next
        bool            g_loadedSinceName = true;
        u16             g_askedLevel = 0;   // (world << 8) | number the game asked for
        u16             g_givenLevel = 0;   // what it got instead
        volatile u32    g_loads = 0;        // levels loaded so far, bumped by the game thread
        volatile u32    g_activity = 0;     // bumped by every hook that did something
        u32             g_activitySeen = 0;
        u32             g_quietTicks = 0;
        bool            g_reportDue = false;

        MenuEntry       *g_eEnabled, *g_eEnemies, *g_eLevels, *g_eCastles, *g_eMode, *g_eReroll, *g_eSeed;
        MenuEntry       *g_eAutosave, *g_eNoRepeats;
        volatile bool   g_inLevel = false;          // between the start of a visit and the way back to the map
        volatile int    g_livesAtEntry = -1;
        volatile bool   g_saveThisVisit = false;    // not after towers and castles, there the game asks by itself
        volatile bool   g_levelRunning = false;     // the level of this visit has had actors
        volatile u32    g_emptyReadings = 0;

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

            if (Test::LevelSeed(seed))
                return seed;
            if (g_set.rerollEachTry)
                seed += RollNumber() * 0x85EBCA6Bu;
            return seed;
        }

        // 0 for bank A, 1 for bank B, -1 for anything else
        int     VramBankOf(u32 addr)
        {
            if (addr >= kVramA && addr < kVramB)
                return 0;
            if (addr >= kVramB && addr < kVramEnd)
                return 1;
            return -1;
        }

        void    *VramAllocHook(void *self, u32 size, s32 alignment)
        {
            HookContext &ctx = HookContext::GetCurrent();
            void        *memory = ctx.OriginalFunction<void *>(self, size, alignment);
            int         bank = VramBankOf(reinterpret_cast<u32>(memory));

            if (bank >= 0 && !g_vramBank[bank])
                g_vramBank[bank] = self;
            if (!memory && size)
            {
                void    *other = self == g_vramBank[0] ? g_vramBank[1] : self == g_vramBank[1] ? g_vramBank[0] : nullptr;

                if (other)
                    memory = ctx.OriginalFunction<void *>(other, size, alignment);
                if (memory)
                    ++g_vramMoved;
                else if (g_vramFailed++ < 4)
                {
                    char    line[112];

                    std::snprintf(line, sizeof(line), " VRAM (t %lus): no room for %lu bytes in either bank, the game is about to freeze\n",
                                  Uptime(), size);
                    AppendLog(line);
                }
            }
            return memory;
        }

        // Memory that came from the other bank has to go back to the allocator it belongs to
        void    VramFreeHook(void *self, void *memory)
        {
            HookContext &ctx = HookContext::GetCurrent();
            int         bank = VramBankOf(reinterpret_cast<u32>(memory));

            if (bank >= 0 && g_vramBank[bank])
                self = g_vramBank[bank];
            ctx.OriginalFunction<void>(self, memory);
        }

        bool    VramPooled(void)
        {
            return g_vramAllocFn && g_vramFreeFn;
        }

        // Fetches the course file of another area the way the game does it
        const u8    *FindCourse(u32 index)
        {
            // the game's string class: vtable, pointer to the characters, capacity, the characters
            struct
            {
                u32     vtable;
                char    *text;
                u32     capacity;
                char    buffer[0x28];
            }           name;
            u32         size = 0;

            if (!g_lookupFn || !g_lookupMgr || !g_nameVtable)
                return nullptr;
            name.vtable = g_nameVtable;
            name.text = name.buffer;
            name.capacity = sizeof(name.buffer);
            std::snprintf(name.buffer, sizeof(name.buffer), "course/course%lu.bin", index + 1);
            return const_cast<HookContext &>(g_lookupHook.GetContext()).OriginalFunction<const u8 *>(g_lookupMgr, &name,
                                                                                                    &size);
        }

        // The game loads what all areas of a level list in one go, so the enemies are planned for
        // the level as a whole, as soon as its first area shows up
        void    PlanWholeLevel(const u8 *first, u32 firstSize)
        {
            const u8    *courses[kAreaCount] = { first, nullptr, nullptr, nullptr };
            u32         sizes[kAreaCount] = { firstSize, 0, 0, 0 };

            for (u32 i = 1; i < kAreaCount; ++i)
            {
                courses[i] = FindCourse(i);
                sizes[i] = courses[i] ? Rando::CourseSize(courses[i]) : 0;
                if (!sizes[i])
                    courses[i] = nullptr;
            }
            Rando::PlanLevel(courses, sizes, g_set, LevelSeed(g_levelKey), VramPooled());
        }

        void    LoadAreaHook(u32 *area, const u8 *course)
        {
            HookContext &ctx = HookContext::GetCurrent();
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
                    std::memset(g_original, 0, sizeof(g_original));
                    ++g_loads;
                    if (g_set.enabled && size)
                        PlanWholeLevel(course, size);
                }
                ++g_activity;

                Rando::Settings set = g_set;

                if (!Test::AreaEnemies(index))
                    set.enemies = 0;
                if (set.enabled && (set.enemies || set.levels || Rando::DropSecretExit()) && size
                    && Rando::RandomizeCourse(course, size, g_course[index], kCourseCap, set,
                                              LevelSeed(g_levelKey), index, g_result[index]))
                {
                    use = g_course[index];
                    g_resultValid[index] = true;
                    g_original[index] = course;
                }
            }

            if (Test::Active() && index < kAreaCount)
            {
                char    note[64];

                std::snprintf(note, sizeof(note), "Course %lu loaded, try %lu", index + 1, g_tries);
                Test::Note(note);
                if (index == 0)
                    Test::MoveStart(const_cast<u8 *>(use));
            }

            ctx.OriginalFunction<void>(area, use);
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
                // for the autosave: was a life lost by the time Mario is back on the map
                g_livesAtEntry = SavedLives();
                g_saveThisVisit = number != 14 && number != 16;
                g_levelRunning = false;
                g_emptyReadings = 0;
                g_inLevel = true;

                Rando::SetProgress(g_set.noRepeats ? &g_progress : nullptr);

                bool    shuffled = g_set.enabled && g_set.levels
                                && Rando::ShuffleLevel(g_set, RollNumber(), world, number, g_givenLevel, world, number);

                // a kind of level ran out and starts over, the list on the card has to follow
                if (shuffled && Rando::ProgressReset())
                    g_configDue = true;

                if (shuffled)
                {
                    worldIndex = world - 1;
                    levelIndex = number - 1;
                }
                Rando::SetNodeShuffled(shuffled || Test::ForcedLevel() != 0);
                if (Test::ForcedLevel())
                {
                    world = Test::ForcedLevel() >> 8;
                    number = Test::ForcedLevel() & 0xFF;
                    worldIndex = world - 1;
                    levelIndex = number - 1;
                }
                g_givenLevel = static_cast<u16>((world << 8) | number);

                if (Test::DropSecret())
                {
                    u32 w = 0, n = 0;

                    for (u32 t = 0; t < 1000 && !Rando::DropSecretExit(); ++t)
                        Rando::ShuffleLevel(g_set, t, 1, 1, 0, w, n);
                }
                if (Test::Active())
                {
                    u8      *stage = *reinterpret_cast<u8 **>(0x005E6288);
                    char    note[64];
                    u32     area, entrance;

                    // the name is only asked when a visit begins, never on the way between areas
                    // (+0x1A and +0x1B are where the game takes the area and entrance to start in from)
                    if (Test::StartArea(area, entrance))
                    {
                        stage[0x1A] = static_cast<u8>(area);
                        stage[0x1B] = static_cast<u8>(entrance);
                    }

                    std::snprintf(note, sizeof(note), "Name asked, try %lu, area %u entrance %u", g_tries,
                                  stage[0x1A], stage[0x1B]);
                    Test::Note(note);
                }
            }

            HookContext::GetCurrent().OriginalFunction<void>(out, type, worldIndex, levelIndex);
        }

        void    SetTimeHook(u32 *timer, u32 seconds)
        {
            g_timer = timer;
            HookContext::GetCurrent().OriginalFunction<void>(timer, seconds);
        }

        void    *FindFileHook(void *mgr, void **nameObj, u32 *outSize)
        {
            u32     size = 0;
            void    *data = HookContext::GetCurrent().OriginalFunction<void *>(mgr, nameObj,
                                                                               outSize ? outSize : &size);

            if (outSize)
                size = *outSize;
            if (nameObj && reinterpret_cast<u32>(nameObj[1]) >= 0x00100000
                && Process::CheckAddress(reinterpret_cast<u32>(nameObj[1]), MEMPERM_READ)
                && std::strncmp(static_cast<const char *>(nameObj[1]), "course/course", 13) == 0)
            {
                // what a lookup of our own needs, see FindCourse
                g_lookupMgr = mgr;
                g_nameVtable = reinterpret_cast<u32>(nameObj[0]);
                if (Test::Active())
                    Test::Note(static_cast<const char *>(nameObj[1]));
            }
            return data;
        }

        void    LoadConfig(void)
        {
            ConfigFile  cfg;
            File        file(kConfigFile, File::READ);

            Rando::DefaultSettings(g_set);
            std::memset(&g_progress, 0, sizeof(g_progress));
            if (file.IsOpen() && file.Read(&cfg, sizeof(cfg)) == 0 && cfg.magic == kConfigMagic
                && cfg.version == kConfigVersion)
            {
                g_set = cfg.settings;
                g_progress = cfg.progress;
            }
            // the item rain is not finished, only the self test may switch it on
            g_set.itemRain = 0;
        }

        void    SaveConfig(void)
        {
            ConfigFile  cfg;

            std::memset(&cfg, 0, sizeof(cfg));
            cfg.magic = kConfigMagic;
            cfg.version = kConfigVersion;
            cfg.settings = g_set;
            cfg.progress = g_progress;

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
            g_eCastles->Name() = std::string("Random towers and castles: ") + OnOff(g_set.castles);
            g_eMode->Name() = std::string("Enemy mix: ") + modes[g_set.mode > 2 ? 0 : g_set.mode];
            g_eReroll->Name() = std::string("New roll after every death: ") + OnOff(g_set.rerollEachTry);
            g_eAutosave->Name() = std::string("Autosave after a level: ") + OnOff(g_set.autosave);
            g_eNoRepeats->Name() = std::string("No finished level twice: ") + OnOff(g_set.noRepeats);
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
                             "Chaos: everything on land trades places" });
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

        u32     *LivesCounter(u32 which);

        // The lives as they are saved, -1 if unknown. Only read, may be called from any thread.
        int     SavedLives(void)
        {
            u32 *lives = LivesCounter(0);

            return lives ? static_cast<int>(*lives) : -1;
        }

        // One of the places the game keeps Mario's lives in, nullptr if it cannot be told for sure
        u32     *LivesCounter(u32 which)
        {
            u32 holder = kLivesHolders[which][0];

            if (g_loader != kLoaderEur || !Process::CheckAddress(holder, MEMPERM_READ))
                return nullptr;

            u32 record = *reinterpret_cast<u32 *>(holder);
            u32 addr = record + kLivesHolders[which][1];

            if (!record || !Process::CheckAddress(addr, MEMPERM_READ | MEMPERM_WRITE))
                return nullptr;

            u32 *lives = reinterpret_cast<u32 *>(addr);

            // anything outside the range the game allows means this is not the counter
            return *lives <= kLivesLimit ? lives : nullptr;
        }

        // Returns the number of lives before, -1 if no counter was found
        int     SetLives(u32 value)
        {
            int before = -1;

            for (u32 i = 0; i < sizeof(kLivesHolders) / sizeof(kLivesHolders[0]); ++i)
            {
                u32 *lives = LivesCounter(i);

                if (!lives)
                    continue;
                if (before < 0)
                    before = static_cast<int>(*lives);
                *lives = value;
            }
            return before;
        }

        bool    SetLives999(void)
        {
            return SetLives(999) >= 0;
        }

        // How many lives Mario has right now, -1 if that cannot be told
        int     CurrentLives(void)
        {
            for (u32 i = sizeof(kLivesHolders) / sizeof(kLivesHolders[0]); i-- > 0; )
            {
                u32 *lives = LivesCounter(i);

                if (lives)
                    return static_cast<int>(*lives);
            }
            return -1;
        }

        void    ChooseLives(MenuEntry *entry)
        {
            int before = CurrentLives();

            if (before < 0)
            {
                MessageBox("The lives could not be found.\nThis works on the European version once a save file is loaded.")();
                return;
            }

            Keyboard    kb("How many lives should Mario have?\nNow: " + std::to_string(before)
                           + ". The game counts up to 1110.");
            u32         value = 99;

            kb.IsHexadecimal(false);
            if (kb.Open(value, value) != 0)
                return;
            if (value > kLivesLimit)
                value = kLivesLimit;
            SetLives(value);
            MessageBox("Lives: " + std::to_string(before) + " -> " + std::to_string(value))();
        }

        bool    SameCode(u32 addr, const u32 *head, u32 words)
        {
            if (!Process::CheckAddress(addr, MEMPERM_READ) || !Process::CheckAddress(addr + words * 4 - 4, MEMPERM_READ))
                return false;
            return std::memcmp(reinterpret_cast<const void *>(addr), head, words * 4) == 0;
        }

        // Saves the game the way its own pause menu does. Returns a word for the log.
        const char  *SaveGame(void)
        {
            if (g_loader != kLoaderEur || !SameCode(kCommitProgressEur, kCommitProgressHead, 4)
                || !SameCode(kCollectSaveEur, kCollectSaveHead, 4) || !SameCode(kRequestSaveEur, kRequestSaveHead, 4))
                return "not supported on this version of the game";
            if (!Process::CheckAddress(kGameEur, MEMPERM_READ) || !Process::CheckAddress(kSaveMgrEur, MEMPERM_READ))
                return "game objects not found";

            u32 game = *reinterpret_cast<u32 *>(kGameEur);
            u32 saveMgr = *reinterpret_cast<u32 *>(kSaveMgrEur);

            if (!game || !saveMgr || !Process::CheckAddress(game + kSlotOffset, MEMPERM_READ)
                || !Process::CheckAddress(saveMgr + kSaveBusyOffset, MEMPERM_READ))
                return "game objects not ready";
            if (*reinterpret_cast<u8 *>(saveMgr + kSaveBusyOffset))
                return "the game is saving already";

            u32 slot = *reinterpret_cast<u8 *>(game + kSlotOffset);

            if (slot > 2)
                return "no save file in use";
            reinterpret_cast<void (*)(u32)>(kCommitProgressEur)(game);
            reinterpret_cast<void (*)(u32, u32, u32)>(kCollectSaveEur)(saveMgr, slot, 0);
            return reinterpret_cast<u32 (*)(u32)>(kRequestSaveEur)(saveMgr) ? "saved" : "the save thread did not take it";
        }

        bool    SaveNow(void)
        {
            const char  *result = SaveGame();
            char        line[96];

            std::snprintf(line, sizeof(line), " Save (t %lus): %s\n", Uptime(), result);
            AppendLog(line);
            return std::strcmp(result, "saved") == 0;
        }

        // Mario left the level of this visit without losing a life: it counts as finished
        void    LevelFinished(void)
        {
            u16     level = g_givenLevel;
            char    line[112];

            if (!g_set.enabled || Rando::LevelSlot(level) < 0 || Rando::IsDone(g_progress, level))
                return;
            Rando::MarkDone(g_progress, level);
            g_configDue = true;
            std::snprintf(line, sizeof(line), " Done (t %lus): %s is finished, %lu of its kind left\n", Uptime(),
                          LevelFile(level).c_str(), Rando::LeftOfItsKind(g_progress, level));
            AppendLog(line);
        }

        void    ForgetFinished(MenuEntry *entry)
        {
            if (!MessageBox("Forget which levels were finished?",
                            "Every level can come up again after that.", DialogType::DialogYesNo)())
                return;
            std::memset(&g_progress, 0, sizeof(g_progress));
            SaveConfig();
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
            if (g_askedLevel && g_askedLevel != g_givenLevel)
                text += "Level file " + LevelFile(g_askedLevel) + " was replaced by " + LevelFile(g_givenLevel) + "\n";
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
            const u64   kMaxLog = 512 * 1024;
            bool        restart = false;

            {
                File    probe(kLogFile, File::READ);

                restart = probe.IsOpen() && probe.GetSize() > kMaxLog;
            }

            File    file(kLogFile, File::RWC | (restart ? File::TRUNCATE : File::APPEND));

            file.Write(text.c_str(), text.size());
        }

        // Bytes free in one VRAM bank, read from the game's allocator. The game thread may be
        // changing the list at the same moment, so every step is checked and the walk is bounded.
        u32     VramFreeBytes(u32 bank)
        {
            u32 self = reinterpret_cast<u32>(g_vramBank[bank]);
            u32 total = 0;

            if (!self || !Process::CheckAddress(self + kVramFreeList, MEMPERM_READ))
                return 0;

            u32 node = *reinterpret_cast<u32 *>(self + kVramFreeList);

            for (u32 n = 0; node && n < 512 && Process::CheckAddress(node + 0x0C, MEMPERM_READ); ++n)
            {
                total += *reinterpret_cast<u32 *>(node + 0x0C);
                node = *reinterpret_cast<u32 *>(node + 0x08);
            }
            return total;
        }

        // Bytes free in one of the game's heaps, 0 if the table is not where EUR 1.0 has it
        u32     HeapFreeBytes(u32 slot, const char *name)
        {
            u32 entry = kHeapTableEur + slot * 4;

            if (!Process::CheckAddress(entry, MEMPERM_READ))
                return 0;

            u32 heap = *reinterpret_cast<u32 *>(entry);

            if (!heap || !Process::CheckAddress(heap + 0x7C, MEMPERM_READ))
                return 0;

            u32 text = *reinterpret_cast<u32 *>(heap + 0x14);

            if (!Process::CheckAddress(text, MEMPERM_READ) || std::strncmp(reinterpret_cast<const char *>(text), name, 16) != 0)
                return 0;

            u32 node = *reinterpret_cast<u32 *>(heap + 0x78);
            u32 count = *reinterpret_cast<u32 *>(heap + 0x7C);
            u32 total = 0;

            for (u32 n = 0; n < count && n < 512 && Process::CheckAddress(node + 0x08, MEMPERM_READ); ++n)
            {
                total += *reinterpret_cast<u32 *>(node + 0x08);
                node = *reinterpret_cast<u32 *>(node + 0x04);
            }
            return total;
        }

        // What the plan for the level decided
        std::string PlanReport(void)
        {
            const Rando::Plan   &plan = Rando::LastPlan();
            std::string         text;
            char                line[128];

            if (!g_set.enabled || !g_set.enemies)
                return text;
            if (!plan.known)
                return " Plan: level not in the memory table, enemies only trade places with what it loads\n";

            if (g_set.itemRain)
                text = plan.rain ? " Plan: item rain is on\n" : " Plan: no room for the item rain in this level\n";
            text += " Plan: " + std::to_string(plan.freshCount) + " new kinds";
            for (u32 i = 0; i < plan.freshCount; ++i)
                text += std::string(i ? ", " : ": ") + KindName(plan.fresh[i]);
            std::snprintf(line, sizeof(line), "\n Plan: free KB before/after, actors %u/%u models %u/%u VRAM A %u/%u B %u/%u\n",
                          plan.freeBefore[Rando::MEM_ACTOR], plan.freeAfter[Rando::MEM_ACTOR],
                          plan.freeBefore[Rando::MEM_MODEL], plan.freeAfter[Rando::MEM_MODEL],
                          plan.freeBefore[Rando::MEM_VRAM_A], plan.freeAfter[Rando::MEM_VRAM_A],
                          plan.freeBefore[Rando::MEM_VRAM_B], plan.freeAfter[Rando::MEM_VRAM_B]);
            return text + line;
        }

        // What the game has free right now, and how low it got since the line before
        std::string MemoryReport(void)
        {
            std::string text;
            char        line[160];

            std::snprintf(line, sizeof(line), " Memory (t %lus): free KB, actors %lu models %lu VRAM A %lu B %lu, lowest since the last line: actors %lu VRAM %lu\n",
                          Uptime(), HeapFreeBytes(0, "ActorHeap") / 1024, HeapFreeBytes(1, "ModelResourceHeap") / 1024,
                          VramFreeBytes(0) / 1024, VramFreeBytes(1) / 1024,
                          g_lowActors == 0xFFFFFFFF ? 0 : g_lowActors / 1024,
                          g_lowVram == 0xFFFFFFFF ? 0 : g_lowVram / 1024);
            text = line;
            if (g_vramMoved || g_vramFailed)
            {
                std::snprintf(line, sizeof(line), " Memory: %lu VRAM requests went to the other bank so far, %lu found no room at all\n",
                              g_vramMoved, g_vramFailed);
                text += line;
            }
            g_loggedActors = HeapFreeBytes(0, "ActorHeap");
            g_lowActors = 0xFFFFFFFF;
            g_lowVram = 0xFFFFFFFF;
            g_vramMovedSeen = g_vramMoved;
            return text;
        }

        // Course files are byte streams, the original one can sit at any address
        u32     Peek16(const u8 *p)
        {
            return p[0] | (p[1] << 8);
        }

        u32     Peek32(const u8 *p)
        {
            return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<u32>(p[3]) << 24);
        }

        // Every sprite of an area that is not what it was, with its place in tiles, and the
        // kinds the area loads on top of its own
        std::string AreaDetails(u32 index)
        {
            const u8    *before = g_original[index];
            const u8    *after = g_course[index];
            std::string text;
            char        line[96];

            if (!before || !Process::CheckAddress(reinterpret_cast<u32>(before), MEMPERM_READ))
                return text;

            u32 sprites = Peek32(before + 7 * 8);
            u32 size = Peek32(before + 7 * 8 + 4);
            u32 listed = 0;

            for (u32 i = 0; (i + 1) * 24 <= size && listed < 160; ++i)
            {
                const u8    *a = before + sprites + i * 24;
                const u8    *b = after + sprites + i * 24;
                u32         was = Peek16(a);

                if (was == 0xFFFF)
                    break;
                if (std::memcmp(a, b, 24) == 0)
                    continue;
                ++listed;
                if (Peek16(b) != was)
                    std::snprintf(line, sizeof(line), "   #%lu at %lu,%lu: %s -> %s\n", i, Peek16(a + 2) / 16, Peek16(a + 4) / 16,
                                  KindName(static_cast<u16>(was)).c_str(), KindName(static_cast<u16>(Peek16(b))).c_str());
                else
                    std::snprintf(line, sizeof(line), "   #%lu at %lu,%lu: %s changed in place\n", i, Peek16(a + 2) / 16,
                                  Peek16(a + 4) / 16, KindName(static_cast<u16>(was)).c_str());
                text += line;
            }

            // what the load list gained
            u32         oldList = Peek32(before + 8 * 8), oldSize = Peek32(before + 8 * 8 + 4);
            u32         newList = Peek32(after + 8 * 8), newSize = Peek32(after + 8 * 8 + 4);
            std::string gained;

            for (u32 n = 0; n + 4 <= newSize; n += 4)
            {
                u32     id = Peek16(after + newList + n);
                bool    had = false;

                for (u32 o = 0; o + 4 <= oldSize && !had; o += 4)
                    had = Peek16(before + oldList + o) == id;
                if (!had)
                    gained += std::string(gained.empty() ? "" : ", ") + KindName(static_cast<u16>(id));
            }
            if (!gained.empty())
                text += "   loads on top: " + gained + "\n";
            if (g_result[index].rain)
                text += "   item rain: " + std::to_string(g_result[index].rain) + " spawners\n";
            return text;
        }

        void    ReportLevel(void)
        {
            u32         swapped = 0;
            std::string log;
            std::string note;
            char        line[128];

            std::snprintf(line, sizeof(line), "Level %08lX file %s -> %s try %lu seed %lu, t %lus\n",
                          g_levelKey, LevelFile(g_askedLevel).c_str(), LevelFile(g_givenLevel).c_str(), g_tries,
                          g_set.seed, Uptime());
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
                log += AreaDetails(i);
            }
            log += PlanReport();
            AppendLog(log);
            // the level is still being set up at this point, its memory is read once it runs
            g_memoryDue = kMemoryDelay;

            if (g_askedLevel != g_givenLevel)
                note = "new level";
            if (swapped)
                note += std::string(note.empty() ? "" : ", ") + std::to_string(swapped) + " enemies";
            if (g_set.notify && !note.empty())
                OSD::Notify("Randomizer: " + note);     // only the self test turns this on
        }

        // Runs on every pass of the plugin loop. Message and log come from here so the
        // game thread never touches files inside the hooks.
        void    OnTick(void)
        {
            Test::OnFrame(g_loads);

            if (g_configDue)
            {
                g_configDue = false;
                SaveConfig();
            }

            // once the game has been drawing for a few seconds, not in the self test
            if (g_ticks == kWelcomeTick && !Test::Active())
                Welcome();

            // twice a second: how low do the pools get while a level is played
            if ((++g_ticks & 31) == 0)
            {
                u32 actors = HeapFreeBytes(0, "ActorHeap");

                if (actors && actors < g_lowActors)
                    g_lowActors = actors;

                // Autosave: the level of this visit ran and has been gone for a while now, so Mario
                // is back on the map. A life less than before means he died or gave up, and then
                // nothing is saved.
                if (g_inLevel && !g_reportDue && g_loader == kLoaderEur
                    && Process::CheckAddress(kHeapTableEur + kAreaHeapSlot * 4, MEMPERM_READ))
                {
                    if (*reinterpret_cast<u32 *>(kHeapTableEur + kAreaHeapSlot * 4))
                    {
                        g_levelRunning = true;
                        g_emptyReadings = 0;
                    }
                    else if (g_levelRunning && ++g_emptyReadings >= kEmptyReadings)
                    {
                        int lives = SavedLives();

                        g_inLevel = false;
                        g_levelRunning = false;
                        g_emptyReadings = 0;
                        if (lives >= 0 && g_livesAtEntry >= 0 && lives >= g_livesAtEntry)
                            LevelFinished();
                        if (g_set.enabled && g_set.autosave && g_saveThisVisit && lives >= 0 && g_livesAtEntry >= 0)
                        {
                            if (lives >= g_livesAtEntry)
                                SaveNow();
                            else
                                AppendLog(" Save: skipped, a life was lost in that level\n");
                        }
                    }
                }
                else if (g_reportDue)
                    g_emptyReadings = 0;
                // Once the actor heap runs low, every further clear drop gets a line: should the
                // game run out, the log shows the way down. Not earlier, writing to the card takes
                // a moment and that moment would be felt in the middle of a level.
                if (actors && actors < kMemoryWatch && !g_memoryDue && !g_reportDue
                    && actors + kMemoryStep < g_loggedActors)
                    g_memoryDue = 1;
                if (g_vramBank[0] && g_vramBank[1])
                {
                    u32 vram = VramFreeBytes(0) + VramFreeBytes(1);

                    if (vram < g_lowVram)
                        g_lowVram = vram;
                }
            }

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

            // Doors and pipes: whenever the game's record of where Mario is changes, the log gets
            // a line, and a reading of the memory three seconds later
            if (g_loader == kLoaderEur && g_loads && (g_ticks & 7) == 0 && Process::CheckAddress(kStageEur, MEMPERM_READ))
            {
                u32 stage = *reinterpret_cast<u32 *>(kStageEur);

                if (stage && Process::CheckAddress(stage + 0x1C, MEMPERM_READ))
                {
                    const u8    *record = reinterpret_cast<const u8 *>(stage);
                    u32         where = (record[0x13] << 24) | (record[0x14] << 16) | (record[0x1A] << 8) | record[0x1B];

                    if (where != g_where && g_where != 0xFFFFFFFF && !g_reportDue)
                    {
                        char    line[112];

                        std::snprintf(line, sizeof(line), " Stage (t %lus): in area %u from entrance %u, heading for area %u entrance %u\n",
                                      Uptime(), record[0x13] + 1, record[0x14], record[0x1A] + 1, record[0x1B]);
                        AppendLog(line);
                        if (!g_memoryDue)
                            g_memoryDue = 180;
                    }
                    g_where = where;
                }
            }

            // A reading of the memory some seconds into the level, and another one whenever a
            // VRAM bank has run over since
            if (g_memoryDue && --g_memoryDue == 0)
                AppendLog(MemoryReport());
            else if (!g_memoryDue && !g_reportDue && g_vramMoved != g_vramMovedSeen)
                AppendLog(MemoryReport());
        }

        // The very first time the game is started with the plugin: who made this and where bug
        // reports go. It can be read again any time in the menu, under "About".
        void    Welcome(void)
        {
            if (File::Exists(kSeenFile) == 1)
                return;
            MessageBox("NSMB2 Randomizer", std::string(kMadeBy) + "\n\nPress Select in the game to open the menu.")();

            File    file(kSeenFile, File::RWC | File::TRUNCATE);

            file.Write("1", 1);
        }

        void    ShowAbout(MenuEntry *entry)
        {
            MessageBox("NSMB2 Randomizer", kMadeBy)();
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
                "Entering a level loads a different one of the same kind: a place with a secret path gets a "
                "level with a secret exit, every other place an ordinary level. Toad houses and cannons stay put. "
                "Checkpoint flags are gone in those levels, a checkpoint from another level would lead nowhere." + next);
            g_eCastles = Switch(g_set.castles,
                "Towers trade places with towers and the castles of the Koopalings with each other, so a "
                "world can have another tower and end with another castle and another boss. Both castles of "
                "Bowser stay put. Only works while random levels are on." + next);
            g_eMode = new MenuEntry("", nullptr, ChooseMode,
                "Safe: walkers stay walkers, flyers stay flyers, swimmers stay swimmers.\n"
                "Wild: Boos and other flyers also replace walkers.\n"
                "Chaos: everything on land trades with everything. Walkers also drop in for flyers, and "
                "Thwomps, Bullet Bills and Banzai Bills join in. Can get unfair.\n"
                "Fish stay in the water in every mode." + next);
            g_eReroll = Switch(g_set.rerollEachTry,
                "On: enemies and level are rolled again every time you enter a level, so also after "
                "every death.\n"
                "Off: the game stays the same for as long as the seed stays the same.");
            g_eSeed = new MenuEntry("", nullptr, ChooseSeed,
                "The seed decides every roll. The same seed gives the same game again.");
            g_eNoRepeats = Switch(g_set.noRepeats,
                "A level you finished does not come up again until all others of its kind are finished too. "
                "Counts every level you left without losing a life. Only with a new roll after every death, "
                "with a fixed seed every level has its one place anyway.");
            g_eAutosave = Switch(g_set.autosave,
                "Saves the game by itself a few seconds after you are back on the map, whenever you left a "
                "level without losing a life. It is the ordinary save of the game, the one from its pause menu. "
                "After towers and castles the game asks about saving by itself, so those are left to it.");

            menu += g_eEnabled;
            menu += g_eEnemies;
            menu += g_eLevels;
            menu += g_eCastles;
            menu += g_eMode;
            menu += g_eReroll;
            menu += g_eSeed;
            menu += g_eNoRepeats;
            menu += g_eAutosave;
            menu += new MenuEntry("Roll a random seed", nullptr, RollSeed);
            menu += new MenuEntry("Give up this level", nullptr, GiveUp,
                                  "Stuck? This runs the timer down, Mario loses a life and you return to the map.");
            menu += new MenuEntry("Set lives", nullptr, ChooseLives,
                                  "Sets Mario's lives to a number of your choice, for when a shuffled game eats them "
                                  "faster than it should.");
            menu += new MenuEntry("Forget finished levels", nullptr, ForgetFinished,
                                  "Clears the list of finished levels, so every level can come up again.");
            menu += new MenuEntry("Show last level", nullptr, ShowLastLevel,
                                  "Lists what was changed in the level loaded last.");
            menu += new MenuEntry("About", nullptr, ShowAbout, "Who made this and where bug reports go.");
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
        // both or none: memory handed out by one hook has to find its way back through the other
        g_vramFreeFn = Install(g_vramFreeHook, kVramFreePattern, sizeof(kVramFreePattern) / 4, kVramFreeEur,
                               reinterpret_cast<u32>(VramFreeHook));
        if (g_vramFreeFn)
            g_vramAllocFn = Install(g_vramAllocHook, kVramAllocPattern, sizeof(kVramAllocPattern) / 4, kVramAllocEur,
                                    reinterpret_cast<u32>(VramAllocHook));
    }

    void    OnProcessExit(void)
    {
        g_loaderHook.Disable();
        g_nameHook.Disable();
        g_lookupHook.Disable();
        g_timeHook.Disable();
        g_vramAllocHook.Disable();
        g_vramFreeHook.Disable();
    }

    int     main(void)
    {
        PluginMenu  *menu = new PluginMenu("NSMB2 Randomizer", 0, 5, 1,
            "Randomizes enemies and levels of New Super Mario Bros. 2 based on a seed.\n"
            "Every setting applies from the next level on.\n\n"
            "Made by L61r on Discord. Found a bug? Send me a message there.");

        g_startTick = svcGetSystemTick();
        menu->SynchronizeWithFrame(true);
        LoadConfig();
        AppendLog("Plugin 0.5.1 started\n");
        Test::Init(g_set);
        Test::SetGiveUp(RunDownTimer);
        Test::SetLives(SetLives999);
        Test::SetSave(SaveNow);
        if (Test::Active())
        {
            char    line[64];

            std::snprintf(line, sizeof(line), "hooks %08lX %08lX %08lX %08lX %08lX %08lX", g_loader, g_nameFn,
                          g_lookupFn, g_timeFn, g_vramAllocFn, g_vramFreeFn);
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
