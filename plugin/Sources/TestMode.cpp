#ifdef RANDO_TEST

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "TestMode.hpp"

using namespace CTRPluginFramework;

namespace Test
{
    namespace
    {
        const u32   kWidth = 400;
        const u32   kHeight = 240;
        const char  *kLogFile = "nsmb2rando.log";

        Rando::Settings *g_settings = nullptr;
        bool        g_active = false;
        bool        g_pilot = false;
        bool        g_walk = false;
        bool        g_swapAreas = false;
        u32         g_startAt = 0;
        u32         g_areaMask = 0;
        u32         g_startArea = 0;
        u32         g_startEntrance = 0;
        u32         g_destArea = 0;
        u32         g_dest = 0;
        bool        g_haveDest = false;
        u32         g_levelSeed = 0;
        bool        g_haveLevelSeed = false;
        bool        g_dropSecret = false;
        u32         g_touchX = 0;
        u32         g_touchY = 0;
        u32         g_cmdKeys = 0;
        u32         g_cmdTouchX = 0;
        u32         g_cmdTouchY = 0;
        u32         g_cmdHold = 0;
        u32         g_forcedLevel = 0;
        u32         g_frame = 0;
        u32         g_loads = 0;
        u32         g_loadFrame = 0;
        u32         g_shotIndex = 0;
        bool        g_wantShot = false;
        bool        g_haveShot = false;
        u8          g_shot[kWidth * kHeight * 3];
        bool        (*g_giveUp)(void) = nullptr;
        bool        (*g_lives)(void) = nullptr;
        bool        (*g_save)(void) = nullptr;

        // slowest pass of the plugin loop since the last heartbeat, in milliseconds of console time
        u64         g_lastTick = 0;
        u32         g_worstMs = 0;

        const u32   kNotes = 48;
        char        g_notes[kNotes][64];
        volatile u32 g_noteHead = 0;
        u32         g_noteTail = 0;

        void    Log(const std::string &text)
        {
            File    file(kLogFile, File::RWC | File::APPEND);

            file.Write(text.c_str(), text.size());
        }

        bool    Capture(const Screen &screen)
        {
            if (!screen.IsTop || !g_wantShot)
                return false;

            Color   pixel;

            // BMP stores the bottom row first and colours as BGR
            for (u32 y = 0; y < kHeight; ++y)
            {
                u8  *row = g_shot + (kHeight - 1 - y) * kWidth * 3;

                for (u32 x = 0; x < kWidth; ++x)
                {
                    screen.ReadPixel(x, y, pixel);
                    row[x * 3] = pixel.b;
                    row[x * 3 + 1] = pixel.g;
                    row[x * 3 + 2] = pixel.r;
                }
            }
            g_wantShot = false;
            g_haveShot = true;
            return false;
        }

        void    SaveShot(void)
        {
            u8      header[54];
            u32     dataSize = sizeof(g_shot);
            char    name[48];

            std::memset(header, 0, sizeof(header));
            header[0] = 'B';
            header[1] = 'M';
            *reinterpret_cast<u32 *>(header + 2) = sizeof(header) + dataSize;
            *reinterpret_cast<u32 *>(header + 10) = sizeof(header);
            *reinterpret_cast<u32 *>(header + 14) = 40;
            *reinterpret_cast<u32 *>(header + 18) = kWidth;
            *reinterpret_cast<u32 *>(header + 22) = kHeight;
            *reinterpret_cast<u16 *>(header + 26) = 1;
            *reinterpret_cast<u16 *>(header + 28) = 24;
            *reinterpret_cast<u32 *>(header + 34) = dataSize;

            std::snprintf(name, sizeof(name), "shot_%02lu.bmp", g_shotIndex++);

            File    file(name, File::RWC | File::TRUNCATE);

            file.Write(header, sizeof(header));
            file.Write(g_shot, dataSize);
            Log(std::string("Shot ") + name + "\n");
        }

        // One "name=number" setting, shared by autotest.txt and the remote control
        void    Apply(const std::string &key, u32 value)
        {
            Rando::Settings &s = *g_settings;

            if (key == "force") Rando::SetForcedTarget(static_cast<uint16_t>(value));
            else if (key == "addkind") Rando::AddExtraKind(static_cast<uint16_t>(value));
            else if (key == "mode") s.mode = static_cast<uint8_t>(value);
            else if (key == "perinst") s.perInstance = static_cast<uint8_t>(value);
            else if (key == "castles") s.castles = static_cast<uint8_t>(value);
            else if (key == "seed") s.seed = value;
            else if (key == "enabled") s.enabled = static_cast<uint8_t>(value);
            else if (key == "enemies") s.enemies = static_cast<uint8_t>(value);
            else if (key == "levels") s.levels = static_cast<uint8_t>(value);
            else if (key == "reroll") s.rerollEachTry = static_cast<uint8_t>(value);
            else if (key == "level") g_forcedLevel = value;
            else if (key == "swap") g_swapAreas = value != 0;
            else if (key == "startat") g_startAt = value;
            else if (key == "areas") g_areaMask = value;
            else if (key == "area") g_startArea = value;
            else if (key == "entr") g_startEntrance = value;
            else if (key == "destarea") g_destArea = value;
            else if (key == "dest") { g_dest = value; g_haveDest = true; }
            else if (key == "lseed") { g_levelSeed = value; g_haveLevelSeed = true; }
            else if (key == "dropsecret") g_dropSecret = value != 0;
            else if (key == "pilot") g_pilot = value != 0;
            else if (key == "walk") g_walk = value != 0;
            else if (key == "touchx") g_touchX = value;
            else if (key == "touchy") g_touchY = value;
            else if (key == "keys") g_cmdKeys = value;
            else if (key == "hold") g_cmdHold = value;
            else if (key == "tx") g_cmdTouchX = value;
            else if (key == "ty") g_cmdTouchY = value;
            else if (key == "shot") g_wantShot = true;
            else if (key == "giveup") Log(g_giveUp && g_giveUp() ? "Timer run down\n" : "No timer yet\n");
            else if (key == "lives") Log(g_lives && g_lives() ? "Lives set to 999\n" : "Lives not found\n");
            else if (key == "save") Log(g_save && g_save() ? "Save requested\n" : "Save refused\n");
            else if (key == "autosave") s.autosave = static_cast<uint8_t>(value);
            else if (key == "norepeat") s.noRepeats = static_cast<uint8_t>(value);
            else if (key == "rain") s.itemRain = static_cast<uint8_t>(value);
            else if (key == "rainprobe") Rando::SetRainProbe(value);
        }

        // lines of the form name=number
        bool    ApplyFile(const char *path)
        {
            char    text[512];
            u64     size = 0;

            {
                File    file(path, File::READ);

                size = file.GetSize();
                if (!file.IsOpen() || size == 0 || size >= sizeof(text) || file.Read(text, static_cast<u32>(size)) != 0)
                    return false;
            }
            text[size] = 0;

            for (char *line = std::strtok(text, "\r\n"); line; line = std::strtok(nullptr, "\r\n"))
            {
                char    *eq = std::strchr(line, '=');

                if (!eq)
                    continue;
                *eq = 0;
                Apply(line, std::strtoul(eq + 1, nullptr, 0));
            }
            return true;
        }

        // Remote control: cmd.txt in the plugin folder is read and then deleted
        void    PollCommand(void)
        {
            if (!File::Exists("cmd.txt"))
                return;

            g_cmdKeys = 0;
            g_cmdTouchX = 0;
            g_cmdTouchY = 0;
            g_cmdHold = 6;

            bool    ok = ApplyFile("cmd.txt");

            File::Remove("cmd.txt");
            if (ok)
                Log("Command read\n");
        }
    }

    void    Init(Rando::Settings &settings)
    {
        g_settings = &settings;
        if (!ApplyFile("autotest.txt"))
            return;

        g_active = true;
        OSD::Run(Capture);
        Log("Self test started\n");
    }

    void    Note(const char *text)
    {
        if (!g_active || g_noteHead - g_noteTail >= kNotes)
            return;

        char    *slot = g_notes[g_noteHead % kNotes];

        std::strncpy(slot, text, 62);
        slot[62] = 0;
        ++g_noteHead;
    }

    u32     ForcedLevel(void)
    {
        return g_active ? g_forcedLevel : 0;
    }

    bool    SwapAreas(void)
    {
        return g_active && g_swapAreas;
    }

    bool    LevelSeed(u32 &seed)
    {
        if (!g_active || !g_haveLevelSeed)
            return false;
        seed = g_levelSeed;
        return true;
    }

    bool    AreaEnemies(u32 index)
    {
        return !g_active || !g_areaMask || ((g_areaMask >> index) & 1);
    }

    bool    StartArea(u32 &area, u32 &entrance)
    {
        if (!g_active || !g_startArea)
            return false;
        area = g_startArea - 1;
        entrance = g_startEntrance;
        return true;
    }

    bool    DropSecret(void)
    {
        return g_active && g_dropSecret;
    }

    void    MoveStart(u8 *course)
    {
        if (!g_active || !g_startAt || !course)
            return;

        const u32   *head = reinterpret_cast<const u32 *>(course);
        u8          start = course[head[2] + 0x10];
        u8          *entrances = course + head[12];
        u32         count = head[13] / 24;
        u8          *from = nullptr, *to = nullptr;

        for (u32 i = 0; i < count; ++i)
        {
            u8  *e = entrances + i * 24;

            if (e[8] == start)
                to = e;
            if (e[8] == g_startAt)
                from = e;
        }
        if (from && to)
            std::memcpy(to, from, 4);
        // and that door may lead somewhere else: destination area and entrance
        if (from && g_haveDest)
        {
            from[9] = static_cast<u8>(g_destArea);
            from[10] = static_cast<u8>(g_dest);
        }
    }

    void    SetSave(bool (*action)(void))
    {
        g_save = action;
    }

    void    SetLives(bool (*action)(void))
    {
        g_lives = action;
    }

    void    SetGiveUp(bool (*action)(void))
    {
        g_giveUp = action;
    }

    bool    Active(void)
    {
        return g_active;
    }

    void    OnFrame(u32 loads)
    {
        if (!g_active)
            return;

        ++g_frame;
        if (loads != g_loads)
        {
            g_loads = loads;
            g_loadFrame = g_frame;
        }

        // A stall inside the game shows up as a long pass in console time. A stall of the
        // emulator itself (compiling shaders) does not, the console clock stands still for it.
        u64 now = svcGetSystemTick();

        if (g_lastTick)
        {
            u32 ms = static_cast<u32>((now - g_lastTick) * 1000 / SYSCLOCK_ARM11);

            if (ms > g_worstMs)
                g_worstMs = ms;
        }
        g_lastTick = now;

        if (g_frame % 300 == 0)
        {
            char    line[64];

            std::snprintf(line, sizeof(line), "Frame %lu levels %lu worst %lu ms\n", g_frame, g_loads, g_worstMs);
            Log(line);
            g_worstMs = 0;
        }

        while (g_noteTail != g_noteHead)
            Log(std::string("Note ") + g_notes[g_noteTail++ % kNotes] + "\n");

        if (g_haveShot)
        {
            g_haveShot = false;
            SaveShot();
        }

        if (g_frame % 20 == 0)
            PollCommand();
        if (g_cmdHold)
        {
            --g_cmdHold;
            if (g_cmdKeys)
                Controller::InjectKey(g_cmdKeys);
            if (g_cmdTouchX)
                Controller::InjectTouch(static_cast<u16>(g_cmdTouchX), static_cast<u16>(g_cmdTouchY));
        }

        u32 since = g_frame - g_loadFrame;

        if (g_pilot)
        {
            u32 keys = 0;

            // A at regular intervals gets through title, file select and intro to the world map
            if (g_frame > 240 && g_frame % 50 < 4)
                keys |= Key::A;
            if (g_walk && g_loads && since > 320 && since < 440)
                keys |= Key::DPadRight;
            if (keys)
                Controller::InjectKey(keys);
            // some screens want a touch, the position comes from autotest.txt
            if (g_touchX && g_frame > 240 && g_frame % 50 >= 25 && g_frame % 50 < 29)
                Controller::InjectTouch(static_cast<u16>(g_touchX), static_cast<u16>(g_touchY));
        }
    }
}

#endif
