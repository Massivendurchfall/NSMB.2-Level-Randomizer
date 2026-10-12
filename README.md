# NSMB2 Randomizer

A Luma3DS plugin (3GX) that randomizes New Super Mario Bros. 2 while you play.
Nothing on the cartridge or in the installed game is modified, the plugin changes level data in memory as the game loads it.

## What it does

- **Random enemies.** Every enemy is rolled on its own, and only into something that makes sense there: walkers on the ground, flyers above it, swimmers in the water. Safe keeps the three apart, Wild lets flyers replace walkers, Chaos adds Thwomps and Bullet Bills.
- **Random levels.** Entering a level loads a different one. Levels with a secret exit only trade places with each other, and towers, castles, toad houses and cannons stay where they are, so the world map keeps working.
- **Random items.** The power-up blocks of a level trade their contents, so the Star may be where the Fire Flower was.
- **A new roll after every death**, or one fixed game per seed, your choice.
- **Give up this level.** A shuffled game can strand you. This menu entry runs the timer down and takes you back to the map.

Bosses are not part of the mix. Outside their arenas they stand still or break the game.

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

Developed against the European version 1.0 and tested in the Azahar emulator. It has not been confirmed on a real console yet, and other regions are located through code patterns and are untested.

## Settings

| Entry | Meaning |
| --- | --- |
| Randomizer | Master switch |
| Random enemies, levels, items | Each part on its own |
| Enemy mix | Safe, Wild or Chaos |
| New roll after every death | On: everything is rolled again each time you enter a level. Off: the seed alone decides. |
| Seed | The same seed gives the same game |
| Give up this level | Way out of a level you cannot finish |
| Show last level | What was changed in the level loaded last |

Every setting applies from the next level you enter. Each change is also written to `nsmb2rando.log` in the plugin folder, which is the file to attach to a bug report.

## Good to know

- An area never gets many more kinds of enemies than it was built with. The game has no memory for more: loading twice as many kinds made it hang in testing.
- Blocks holding a Mini Mushroom, coins, beanstalks or a Mega Mushroom are never changed, some levels cannot be finished without them.
- Chaos puts Thwomps and Bullet Bills where Goombas used to be. It is meant to be unfair.
- Checkpoint flags are removed while levels are random. A checkpoint sends you back to a spot of the level it was taken in, and the same place on the map may load another level next time.
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

## How it works

The plugin hooks four functions of the game:

- the loader of an area's `courseN.bin`: it receives a copy with other sprite IDs and a matching list of sprites to load
- the function that builds the level file name `W-N`: it gets a different world and level
- the file lookup inside the level archive: the tile layers pass through it, and item blocks are objects in those layers
- the function that sets the level timer: only to learn where the timer lives

No game files are included in this repository.

## Credits

- [CTRPluginFramework](https://gitlab.com/thepixellizeross/ctrpluginframework) and the [blank template](https://github.com/PabloMK7/CTRPluginFramework-BlankTemplate) by Nanquitas and PabloMK7
- [CoinKiller](https://github.com/Arisotura/CoinKiller), whose source documents the level format, sprite IDs and item blocks

New Super Mario Bros. 2 is a trademark of Nintendo. This project is not affiliated with Nintendo.
