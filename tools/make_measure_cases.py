"""Writes the case lists for the memory measurement in the self test emulator (emu_measure.ps1).

    python make_measure_cases.py <Course dir> <out dir>

kinds.txt   one case per enemy kind: the kind is put on the load list of a level that does not
            have it, to see what loading it costs (two base levels, so every kind is new in one)
levels.txt  one case per area of every level that has enemies to swap: the level is started in
            that area with the randomizer idle, to see how much memory the game leaves free there
"""
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nsmb2

course_dir, out_dir = sys.argv[1], sys.argv[2]
core = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'plugin', 'Sources', 'Randomizer.cpp'),
            encoding='utf-8').read()

# enemy kinds and pipe plants straight from the tables of the core
table = core[core.index('kKinds[] ='):core.index('kKindCount')]
kinds = [int(m) for m in re.findall(r'\{\s*(\d+), SLOT_', table)]
plants_src = core[core.index('kPlants[] ='):core.index('kPlantCount')]
plants = [int(m) for m in re.findall(r'\{\s*(\d+),', plants_src)]
swappable = set(kinds) | set(plants)

BASES = (0x050C, 0x0101)        # 5-12 has ghosts and no Koopas, 1-1 the other way round
with open(os.path.join(out_dir, 'kinds.txt'), 'w') as f:
    for base in BASES:
        f.write('level=%d;area=0;addkind=0\n' % base)
        for kind in kinds + plants:
            f.write('level=%d;area=0;addkind=0;addkind=%d\n' % (base, kind))

count = 0
with open(os.path.join(out_dir, 'levels.txt'), 'w') as f:
    for name in sorted(os.listdir(course_dir), key=lambda n: [int(x) for x in n[:-5].split('-')]):
        world, number = (int(x) for x in name[:-5].split('-'))
        data = open(os.path.join(course_dir, name), 'rb').read()
        files = {k: data[a:b] for k, (a, b) in nsmb2.sarc_files(data).items()}
        areas = {}
        for area in range(1, 5):
            course = files.get('course/course%d.bin' % area)
            if course:
                areas[area] = course
        if not any(s['id'] in swappable for c in areas.values() for s in nsmb2.course_sprites(c)):
            continue
        # every entrance something leads to, per area
        arrivals = {a: [] for a in areas}
        own = {}
        for area, course in areas.items():
            off, size = nsmb2.course_blocks(course)[6]
            own[area] = [course[off + i * 24:off + i * 24 + 24] for i in range(size // 24)]
        for area, entrances in own.items():
            for e in entrances:
                dest_area = e[9] if e[9] else area
                if (e[9] or e[10]) and dest_area in arrivals:
                    arrivals[dest_area].append(e[10])
        key = zlib.crc32(areas[1]) & 0xFFFFFFFF if 1 in areas else 0
        for area in sorted(areas):
            ids = [e[8] for e in own[area]]
            if area == 1:
                f.write('level=%d;area=0;addkind=0 # %s area 1 key %08X\n' % ((world << 8) | number, name[:-5], key))
            else:
                good = [x for x in arrivals[area] if x in ids]
                if not good and not ids:
                    continue
                entr = good[0] if good else ids[0]
                f.write('level=%d;area=%d;entr=%d;addkind=0 # %s area %d key %08X\n'
                        % ((world << 8) | number, area, entr, name[:-5], area, key))
            count += 1
print('kinds: %d cases, levels: %d cases' % ((len(kinds) + len(plants) + 1) * len(BASES), count))
