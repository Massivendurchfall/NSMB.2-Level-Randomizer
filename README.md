# NSMB2 Randomizer

A Luma3DS plugin (3GX) that randomizes New Super Mario Bros. 2 while you play.
Nothing on the cartridge or in the installed game is modified, the plugin changes level data in memory as the game loads it.

## What it does

- **Random enemies.** Every enemy is rolled on its own, and only into something that makes sense there: walkers on the ground, flyers above it, swimmers in the water. Safe keeps the three apart, Wild lets flyers replace walkers. In Chaos everything on land trades with everything: walkers also drop in for flyers, and Thwomps and Bullet Bills join in.
- **Random levels.** Entering a level loads a different one of the same kind: a place with a secret path gets a level with a secret exit, so cannons and shortcuts stay reachable, every other place gets an ordinary level. With a new roll on every visit it is never the level that belongs there, never the one that came up last, and none you have finished already until all others of its kind are finished too.
- **Random towers and castles.** Towers trade places with towers and the castles of the Koopalings with each other, so a world can end with another castle and another boss. The three towers with a secret exit only trade among themselves, the cannons of worlds 1 and 3 hang on them. Both castles of Bowser stay put.
- **A new roll after every death**, or one fixed game per seed, your choice.
- **Give up this level.** A shuffled game can strand you. This menu entry runs the timer down and takes you back to the map.
- **Set lives.** A shuffled game also eats lives. This menu entry sets them to any number.
- **Autosave.** The game is saved a few seconds after you are back on the map, whenever you left a level without losing a life. It is the ordinary save from the pause menu of the game, which normally only comes up after towers and castles.

Bosses never show up as ordinary enemies, outside their arenas they stand still or break the game. They only move together with their castle.

## Download

Get the latest [`nsmb2-randomizer.zip`](https://github.com/Massivendurchfall/NSMB.2-Randomizer-Plugin/releases/latest/download/nsmb2-randomizer.zip) from the releases page.

## Install

1. You need a 3DS with Luma3DS and its plugin loader enabled (Rosalina menu, `L + Down + Select`).
2. Unpack `nsmb2-randomizer.zip` onto the root of the SD card. It contains the folder `luma/plugins/` with one subfolder per region:
   - Europe: `000400000007AF00`
   - USA: `000400000007AE00`
   - Japan: `000400000007AD00`
3. Start the game and press **Select** to open the menu.

If you would rather copy a single file, take `nsmb2rando.3gx` from the release and put it into `/luma/plugins/<title id>/` yourself.

Developed against the European version 1.0, tested in the Azahar emulator and played on an Old 3DS XL. Other regions are located through code patterns and are untested. The memory table (see below) only knows the European levels, elsewhere enemies just trade places with what a level already loads.

## Settings

| Entry | Meaning |
| --- | --- |
| Randomizer | Master switch |
| Random enemies, levels, towers and castles | Each part on its own. Towers and castles need levels to be on. |
| Enemy mix | Safe, Wild or Chaos |
| New roll after every death | On: everything is rolled again each time you enter a level. Off: the seed alone decides. |
| Seed | The same seed gives the same game |
| Give up this level | Way out of a level you cannot finish |
| Set lives | Any number of lives, European version only |
| No finished level twice | A level you left alive does not come up again until its kind has run out |
| Autosave after a level | Saves by itself after every level you leave alive, European version only |
| Forget finished levels | Clears that list |
| Show last level | What was changed in the level loaded last |

Every setting applies from the next level you enter. Each level is also written to `nsmb2rando.log` in the plugin folder: what was swapped, which new kinds the level got and how much memory the game had left. That is the file to attach to a bug report.

## Good to know

- New kinds of enemies are budgeted per level against the memory the game really has. The game loads everything the areas of a level list in one go, and some enemies are expensive: the model pool of the Koopa Troopa alone takes a quarter of the actor heap. The plugin carries a table, measured in the emulator, of what every level leaves free and what every kind costs. A level that is nearly full gets no new kinds, its enemies then only trade places with each other.
- The graphics memory of the 3DS comes in two banks, and the game freezes on a black screen with the music still playing once one of them has no room left for the next room of a level. Versions before 0.5.0 ran into that behind some doors and pipes. The plugin now lets a full bank borrow from the other one.
- Item blocks are never touched. Too many levels need the one power-up they were built around, be it a Mini Mushroom, a Mega Mushroom or a Gold Flower.
- Chaos puts Thwomps and Bullet Bills where Goombas used to be, and a Hammer Bro may fall out of the sky where a Paratroopa used to fly. It is meant to be unfair.
- Checkpoint flags are removed in the levels that are shuffled. A checkpoint sends you back to a spot of the level it was taken in, and the same place on the map may load another level next time.
- Star Coins belong to the place on the map, not to the level. The game remembers three per place, so a coin counts as collected if you took the one with the same number there before, whatever level was loaded at the time.
- Gold Rings are removed while enemies are random. A ring makes the game draw every enemy with its gold model, swapped enemies have none, and the game crashes on that.
- Things that never move and cannot be stomped (plants, Amps, Urchins) only replace each other, so a swap cannot seal a passage.

## Building

Requirements: devkitARM with libctru, plus `libctrpf` and `3gxtool` from [CTRPluginFramework](https://gitlab.com/thepixellizeross/ctrpluginframework). `tools/setup_toolchain.sh` installs them into a devkitPro MSYS2.

```bash
tools/build.sh
```

The result is `plugin/nsmb2rando.3gx`.

### Tests

The randomizer core in `plugin/Sources/Randomizer.cpp` does not depend on the plugin framework and is tested on the PC against every course file of the game. You need your own dump of the game for that:

```bash
python tools/decrypt_extract.py <your game.cia> <boot9.bin> game_dump
python tools/make_blob.py game_dump/romfs/Course build_host/courses.blob
tools/host_test.sh
```

`tools/build.sh test` builds a variant with a self test that can be driven inside the Azahar emulator.

### The memory table

`plugin/Includes/MemoryTable.hpp` is generated. The self test can start any level in any of its areas and load a kind of enemy without placing it. `tools/emu_stat.py` then reads the free space of the game's heaps and of both VRAM banks through the GDB stub of the emulator. `tools/make_measure_cases.py` writes the list of cases, `tools/make_memory_table.py` turns the readings into the table.

## How it works

The plugin hooks six functions of the game:

- the loader of an area's `courseN.bin`: it receives a copy with other sprite IDs and a matching list of sprites to load. When the first area of a level arrives, the plugin fetches the other areas as well and plans the enemies for the level as a whole.
- the function that builds the level file name `W-N`: it gets a different world and level
- the file lookup inside the level archive: only to learn how the game calls it, the plugin fetches the other areas of a level the same way
- the function that sets the level timer: only to learn where the timer lives
- the allocator of the VRAM banks and its counterpart that frees: a request a full bank cannot serve goes to the other bank

No game files are included in this repository.

## Credits

- [CTRPluginFramework](https://gitlab.com/thepixellizeross/ctrpluginframework) and the [blank template](https://github.com/PabloMK7/CTRPluginFramework-BlankTemplate) by Nanquitas and PabloMK7
- [CoinKiller](https://github.com/Arisotura/CoinKiller), whose source documents the level format, sprite IDs and item blocks

New Super Mario Bros. 2 is a trademark of Nintendo. This project is not affiliated with Nintendo.
