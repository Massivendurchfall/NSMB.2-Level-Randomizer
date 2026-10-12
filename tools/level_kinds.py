"""Classifies every level file by what it contains (goal pole, boss, toad house, cannon ...)."""
import sys
from collections import defaultdict

import nsmb2

MARKS = {97: 'goal', 38: 'reznor', 102: 'iggy', 141: 'larry', 142: 'lemmy', 155: 'ludwig', 162: 'morton',
         211: 'roy', 245: 'wendy', 125: 'bowser', 128: 'drybowser', 130: 'bigbowser', 103: 'toadhouse',
         294: 'cannon', 22: 'warp', 30: 'bossctl', 315: 'credits', 269: 'goldship', 228: 'boo'}

levels = defaultdict(lambda: {'areas': 0, 'marks': defaultdict(int), 'goals': []})
for level, name, course in nsmb2.iter_courses(sys.argv[1]):
    info = levels[level]
    info['areas'] += 1
    for s in nsmb2.course_sprites(course):
        if s['id'] in MARKS:
            info['marks'][MARKS[s['id']]] += 1
        if s['id'] == 97:
            info['goals'].append(s['raw'][6:16].hex())


def key(name):
    w, l = name.split('-')
    return int(w), int(l)


for level in sorted(levels, key=key):
    info = levels[level]
    marks = ' '.join(f'{k}{"x" + str(v) if v > 1 else ""}' for k, v in sorted(info['marks'].items()))
    print(f"{level:5} areas {info['areas']}  {marks}  {' '.join(info['goals'])}")
