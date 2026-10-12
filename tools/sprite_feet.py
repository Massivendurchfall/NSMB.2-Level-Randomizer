"""Reads the sprite rectangles from CoinKiller's objects.cpp and compares them with real placements.

For every swappable kind this prints where the bottom edge of its editor rectangle sits
relative to the sprite position (in game units, 16 per tile) and how the game's own levels
place it (y modulo 16). The difference between two kinds is the height correction a swap needs.

Usage: python sprite_feet.py <ck_objects.cpp> <romfs/Course>
"""
import re
import sys
from collections import Counter

import nsmb2

IDS = [135, 136, 137, 139, 165, 158, 232, 110, 111, 29, 33, 31, 32, 34, 213, 99, 138, 82, 159,
       175, 176, 177, 178, 179, 180, 185, 228, 229, 140, 8, 113, 184, 16,
       194, 195, 200, 95, 96, 240, 83, 3, 77, 76, 118, 119, 125, 128, 102, 141, 142, 155, 162, 211, 245]


def rects(path):
    text = open(path, encoding='utf-8', errors='replace').read()
    body = text[text.index('void Sprite::setRect'):]
    out = {}
    # every "case a: case b: ... break;" group
    for m in re.finditer(r'((?:case \d+:\s*(?://[^\n]*)?\s*)+)(.*?)break;', body, re.S):
        ids = [int(x) for x in re.findall(r'case (\d+):', m.group(1))]
        block = m.group(2)
        hs = [int(x) for x in re.findall(r'height = (-?\d+);', block)]
        oys = [int(x) for x in re.findall(r'offsety = (-?\d+);', block)]
        for i in ids:
            out.setdefault(i, (hs, oys))
    return out


def main():
    data = rects(sys.argv[1])
    ymod = {i: Counter() for i in IDS}
    for _level, _name, course in nsmb2.iter_courses(sys.argv[2]):
        for s in nsmb2.course_sprites(course):
            if s['id'] in ymod:
                ymod[s['id']][s['y'] % 16] += 1
    for i in IDS:
        hs, oys = data.get(i, ([], []))
        bottoms = sorted({round((h + oy) * 0.8, 1) for h, oy in zip(hs, oys)}) if hs and len(hs) == len(oys) else '?'
        print(f'{i:3}: heights {hs} offsets {oys} bottom {bottoms}  y%16 {dict(ymod[i].most_common(3))}')


if __name__ == '__main__':
    main()
